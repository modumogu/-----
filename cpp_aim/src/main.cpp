// 能量机关自瞄 —— C++ 版 第 1 步：颜色掩膜
//
// 目标：把画面里的红色灯条分离出来，左右并排显示"原图 | 掩膜"
//
// 用法：
//     aim.exe                  用相机（索引 0）
//     aim.exe --video xxx.mp4  用视频文件
//
// 按键：ESC 或 q 退出

#include <opencv2/opencv.hpp>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

// 红色在 HSV 的 H 上跨 0 和 179 两端，所以要两段
struct HsvRange {
    cv::Scalar lo;
    cv::Scalar hi;
};

const HsvRange kRedRanges[] = {
    {cv::Scalar(0, 120, 120), cv::Scalar(10, 255, 255)},
    {cv::Scalar(170, 120, 120), cv::Scalar(179, 255, 255)},
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

}  // namespace

int main(int argc, char** argv) {
    bool use_camera = true;
    bool headless = false;
    std::string video_path;
    std::string save_path;
    int max_frames = 1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--video" && i + 1 < argc) {
            use_camera = false;
            video_path = argv[++i];
        } else if (a == "--save" && i + 1 < argc) {
            // 不弹窗，处理 max_frames 帧后存图退出（用来验证/截图）
            headless = true;
            save_path = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            max_frames = std::stoi(argv[++i]);
        }
    }

    cv::VideoCapture cap;
    if (use_camera) {
        cap.open(0, cv::CAP_DSHOW);
        if (cap.isOpened()) {
            cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y', 'U', 'Y', '2'));
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1024);
            cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.25);  // 关自动
            cap.set(cv::CAP_PROP_EXPOSURE, -8.0);       // 约 3.9 ms
        }
    } else {
        cap.open(video_path);
    }

    if (!cap.isOpened()) {
        std::cerr << "打不开视频源\n";
        return 1;
    }

    std::cout << "分辨率 " << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
              << "   帧率 " << cap.get(cv::CAP_PROP_FPS)
              << "   曝光 " << cap.get(cv::CAP_PROP_EXPOSURE) << "\n";
    std::cout << "ESC / q 退出\n";

    int idx = 0;
    while (true) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) {
            break;  // 视频放完
        }

        cv::Mat mask = buildRedMask(frame);

        // 掩膜转彩色，方便并排显示
        cv::Mat mask_bgr;
        cv::cvtColor(mask, mask_bgr, cv::COLOR_GRAY2BGR);

        cv::Mat both;
        cv::hconcat(frame, mask_bgr, both);

        // 左上角显示统计信息
        char text[128];
        std::snprintf(text, sizeof(text), "frame %d   mask px %d",
                      idx, cv::countNonZero(mask));
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
