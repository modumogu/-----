// 能量机关自瞄 —— C++ 版
//
// 已完成：① 颜色掩膜  ② 轮廓过滤  ③ 质心
// 下一步：④ 槽位归组
//
// 用法：
//     aim.exe                                  自动挑相机
//     aim.exe --index 1                        强制某个相机
//     aim.exe --expo -6                        曝光（-8 最暗，值越大越亮）
//     aim.exe --wb 4600                        色温
//     aim.exe --video xxx.mp4                  用视频
//     aim.exe --video xxx.mp4 --save out.png   存一张图
//
// 按键：ESC 或 q 退出

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX    // 否则 windows.h 的 min/max 宏会把 std::min/std::max 打散
#include <windows.h>
#endif

namespace {

constexpr int kWantW = 1280;
constexpr int kWantH = 1024;

// 灯条的形状范围（像素）
constexpr double kMinLedArea = 20.0;    // 太小是噪点
constexpr double kMaxLedArea = 2500.0;  // 太大是光斑/整屏
constexpr double kMinAspect = 1.3;      // 长宽比，滤掉圆斑

// 红色在 HSV 的 H 上跨 0 和 179 两端，所以要两段
struct HsvRange {
    cv::Scalar lo;
    cv::Scalar hi;
};

const HsvRange kRedRanges[] = {
    {cv::Scalar(0, 120, 120), cv::Scalar(10, 255, 255)},
    {cv::Scalar(170, 120, 120), cv::Scalar(179, 255, 255)},
};

// 一个灯条（一条亮的灯条，不是一整个靶）
struct Led {
    cv::Point2f center;
    double area = 0.0;
    double aspect = 0.0;
};

// ① 颜色掩膜：HSV 双段 → 位或 → 开运算
cv::Mat buildRedMask(const cv::Mat& bgr) {
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);

    cv::Mat mask = cv::Mat::zeros(hsv.size(), CV_8UC1);
    for (const auto& r : kRedRanges) {
        cv::Mat m;
        cv::inRange(hsv, r.lo, r.hi, m);
        cv::bitwise_or(mask, m, mask);
    }

    // 3x3 椭圆开运算去孤立噪点（灯条只有 6~7 像素宽，别用大核）
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    return mask;
}

// ② + ③ 找轮廓 → 面积/长宽比过滤 → 算质心
std::vector<Led> detectLeds(const cv::Mat& mask, int* raw_count) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (raw_count) *raw_count = static_cast<int>(contours.size());

    std::vector<Led> leds;
    leds.reserve(contours.size());
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < kMinLedArea || area > kMaxLedArea) {
            continue;  // 太小是噪点，太大是光斑
        }

        cv::RotatedRect rr = cv::minAreaRect(c);
        double w = rr.size.width, h = rr.size.height;
        double lo = std::min(w, h), hi = std::max(w, h);
        if (lo <= 0.0) continue;
        double aspect = hi / lo;
        if (aspect < kMinAspect) {
            continue;  // 太"方"了，不像灯条
        }

        cv::Moments M = cv::moments(c);
        if (M.m00 <= 0.0) continue;

        Led led;
        led.center = cv::Point2f(static_cast<float>(M.m10 / M.m00),
                                 static_cast<float>(M.m01 / M.m00));
        led.area = area;
        led.aspect = aspect;
        leds.push_back(led);
    }
    return leds;
}

// 笔记本自带摄像头通常是索引 0、最高 640x480；
// USB 工业相机在后面。所以逐个试，谁能给 1280x1024 就用谁。
int openCamera(cv::VideoCapture& cap, int force_index, double exposure, double wb_temp) {
    for (int idx = 0; idx < 5; ++idx) {
        if (force_index >= 0 && idx != force_index) {
            continue;
        }
        cap.open(idx, cv::CAP_DSHOW);
        if (!cap.isOpened()) {
            cap.release();
            continue;
        }
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', '2'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, kWantW);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, kWantH);
        cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);      // 关自动曝光
        cap.set(cv::CAP_PROP_EXPOSURE, exposure);       // -8 ≈ 3.9ms
        cap.set(cv::CAP_PROP_AUTO_WB, 0);               // 关自动白平衡（关键）
        cap.set(cv::CAP_PROP_WB_TEMPERATURE, wb_temp);  // 手动色温

        cv::Mat probe;
        cap >> probe;
        int w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        int h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        if (!probe.empty() && w == kWantW && h == kWantH) {
            return idx;
        }
        std::cout << "  索引 " << idx << " 不行（" << w << "x" << h << "），跳过\n";
        cap.release();
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // 源码是 UTF-8，控制台默认 GBK，不设这个中文输出就是乱码
    SetConsoleOutputCP(CP_UTF8);
#endif

    bool use_camera = true;
    bool headless = false;
    int want_index = -1;
    std::string video_path;
    std::string save_path;
    int max_frames = 1;
    double exposure = -8.0;
    double wb_temp = 4600.0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--video" && i + 1 < argc) {
            use_camera = false;
            video_path = argv[++i];
        } else if (a == "--index" && i + 1 < argc) {
            want_index = std::stoi(argv[++i]);
        } else if (a == "--save" && i + 1 < argc) {
            headless = true;
            save_path = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            max_frames = std::stoi(argv[++i]);
        } else if (a == "--expo" && i + 1 < argc) {
            exposure = std::stod(argv[++i]);
        } else if (a == "--wb" && i + 1 < argc) {
            wb_temp = std::stod(argv[++i]);
        }
    }

    cv::VideoCapture cap;
    if (use_camera) {
        int idx = openCamera(cap, want_index, exposure, wb_temp);
        if (idx < 0) {
            std::cerr << "没找到能跑 1280x1024 的相机。\n";
            return 1;
        }
        std::cout << "选中相机索引 " << idx << "\n";
    } else {
        cap.open(video_path);
        if (!cap.isOpened()) {
            std::cerr << "打不开视频: " << video_path << "\n";
            return 1;
        }
    }

    std::cout << "分辨率 " << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
              << "   帧率 " << cap.get(cv::CAP_PROP_FPS)
              << "   曝光 " << cap.get(cv::CAP_PROP_EXPOSURE)
              << "   色温 " << cap.get(cv::CAP_PROP_WB_TEMPERATURE) << "\n";
    std::cout << "ESC / q 退出\n";

    int idx = 0;
    bool warned_dark = false;
    while (true) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) {
            break;  // 视频放完
        }

        if (!warned_dark && cv::mean(frame)[0] < 5.0) {
            std::cout << "[WARN] 画面几乎全黑（平均灰度 < 5）\n"
                         "       可能原因：曝光太短 / 镜头盖没摘 / 环境太暗\n";
            warned_dark = true;
        }

        cv::Mat mask = buildRedMask(frame);
        int raw_contours = 0;
        std::vector<Led> leds = detectLeds(mask, &raw_contours);

        if (idx % 30 == 0) {
            std::cout << "frame " << idx
                      << "  轮廓 " << raw_contours
                      << "  灯条 " << leds.size() << "\n";
        }

        cv::Mat mask_bgr;
        cv::cvtColor(mask, mask_bgr, cv::COLOR_GRAY2BGR);
        cv::Mat both;
        cv::hconcat(frame, mask_bgr, both);

        // 在原图和掩膜上都画质心
        for (size_t k = 0; k < leds.size(); ++k) {
            const cv::Point2f& p = leds[k].center;
            cv::circle(both, p, 6, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
            cv::circle(both, p + cv::Point2f((float)kWantW, 0.0f), 6,
                       cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
            char tag[16];
            std::snprintf(tag, sizeof(tag), "%d", static_cast<int>(k));
            cv::putText(both, tag, p + cv::Point2f(9, -9),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        }

        char text[160];
        std::snprintf(text, sizeof(text), "frame %d   leds %d   contours %d   mask %d",
                      idx, static_cast<int>(leds.size()), raw_contours,
                      cv::countNonZero(mask));
        cv::putText(both, text, cv::Point(15, 35), cv::FONT_HERSHEY_SIMPLEX,
                    0.8, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

        cv::imshow("frame | red mask", both);
        ++idx;

        if (headless) {
            if (idx >= max_frames) {
                cv::imwrite(save_path, both);
                std::cout << "已存 " << save_path << "\n";
                break;
            }
            continue;
        }

        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q') {
            break;
        }
    }

    cap.release();
    cv::destroyAllWindows();
    std::cout << "处理了 " << idx << " 帧\n";
    return 0;
}
