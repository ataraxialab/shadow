#include "yolo.h"
#include "kernel.h"
#include "parser.h"
#include "util.h"

#include <ctime>

using namespace std;

Yolo::Yolo(string cfgfile, string weightfile, float threshold) {
  cfgfile_ = cfgfile;
  weightfile_ = weightfile;
  threshold_ = threshold;
}

Yolo::~Yolo() {}

void Yolo::Setup(int batch, Box *roi) {
  Kernel::KernelSetup();

  Parser parser;
  parser.ParseNetworkCfg(net_, cfgfile_, batch);
  if (weightfile_.empty())
    error("Weight file is empty!");
  parser.LoadWeights(net_, weightfile_);
  batch_data_ = new float[net_.batch_ * net_.in_num_];
  im_ini_ = new JImage();
  im_crop_ = new JImage();
  im_res_ = new JImage(3, net_.in_h_, net_.in_w_);
  roi_ = roi;
}

void Yolo::Release() {
  net_.ReleaseNetwork();
  Kernel::KernelRelease();
}

void Yolo::Test(string imagefile) {
  clock_t time = clock();
  im_ini_->Read(imagefile);
  vector<JImage *> images(1, im_ini_);
  vector<VecBox> Bboxes(1);
  PredictYoloDetections(images, Bboxes);
  Boxes::BoxesNMS(Bboxes[0], 0.5);
  Boxes::AmendBoxes(Bboxes[0], roi_);
  cout << "Predicted in " << static_cast<float>(clock() - time) / CLOCKS_PER_SEC
       << " seconds" << endl;
  im_ini_->Rectangle(Bboxes[0]);
  im_ini_->Show("result");
}

void Yolo::BatchTest(string listfile, bool image_write) {
  vector<string> imagelist;
  Parser::LoadImageList(imagelist, listfile);
  size_t num_im = imagelist.size();

  clock_t time = clock();
  vector<JImage *> images(num_im);
  vector<VecBox> Bboxes(num_im);
  for (int i = 0; i < num_im; ++i)
    images[i] = new JImage(imagelist[i]);

  PredictYoloDetections(images, Bboxes);

  ofstream file(find_replace_last(listfile, ".", "-result."));
  for (int i = 0; i < num_im; ++i) {
    Boxes::BoxesNMS(Bboxes[i], 0.5);
    Boxes::AmendBoxes(Bboxes[i], roi_);
    if (image_write) {
      string path = find_replace_last(imagelist[i], ".", "-result.");
      images[i]->Rectangle(Bboxes[i], false);
      images[i]->Write(path);
    }
    PrintYoloDetections(file, Bboxes[i], i + 1);
    if (!((i + 1) % 100))
      cout << "Processing: " << i + 1 << " / " << num_im << endl;
  }
  cout << "Processing: " << num_im << " / " << num_im << endl;
  float sec = static_cast<float>(clock() - time) / CLOCKS_PER_SEC;
  cout << "Processed in: " << sec << " seconds, each frame: " << sec / num_im
       << "seconds" << endl;
}

#ifdef USE_OpenCV
void Yolo::VideoTest(string videofile, bool video_show, bool video_write) {
  cv::VideoCapture capture;
  if (!capture.open(videofile))
    error("error when opening video file " + videofile);
  float rate = static_cast<float>(capture.get(cv::CAP_PROP_FPS));
  int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  cv::VideoWriter writer;
  if (video_write) {
    string outfile = find_replace_last(videofile, ".", "-result.");
    outfile = change_extension(outfile, ".avi");
    int format = writer.fourcc('X', '2', '6', '4');
    writer.open(outfile, format, rate, cv::Size(width, height));
  }
  CaptureTest(capture, "yolo video test!-:)", video_show, writer, video_write);
  capture.release();
  writer.release();
}

void Yolo::Demo(int camera, bool video_write) {
  cv::VideoCapture capture;
  if (!capture.open(camera))
    error("error when opening camera!");
  float rate = static_cast<float>(capture.get(cv::CAP_PROP_FPS));
  int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  cv::VideoWriter writer;
  if (video_write) {
    int format = writer.fourcc('X', '2', '6', '4');
    writer.open("./data/demo-result.avi", format, rate,
                cv::Size(width, height));
  }
  CaptureTest(capture, "yolo demo!-:)", true, writer, video_write);
  capture.release();
  writer.release();
}

void Yolo::CaptureTest(cv::VideoCapture capture, string window_name,
                       bool video_show, cv::VideoWriter writer,
                       bool video_write) {
  float rate = static_cast<float>(capture.get(cv::CAP_PROP_FPS));
  if (video_show)
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
  cv::Mat im_mat;
  vector<JImage *> images(1, im_ini_);
  vector<VecBox> Bboxes(1);
  clock_t time;
  VecBox currBoxes;
  while (capture.read(im_mat)) {
    if (im_mat.empty())
      break;
    time = clock();
    images[0]->FromMat(im_mat);
    PredictYoloDetections(images, Bboxes);
    Boxes::BoxesNMS(Bboxes[0], 0.5);
    Boxes::AmendBoxes(Bboxes[0], roi_);
    Boxes::SmoothBoxes(currBoxes, Bboxes[0], 0.3);
    currBoxes = Bboxes[0];
    DrawYoloDetections(im_mat, Bboxes[0], true);
    if (video_write)
      writer.write(im_mat);
    float sec = static_cast<float>(clock() - time) / CLOCKS_PER_SEC;
    int waittime = static_cast<int>(1000.0f * (1.0f / rate - sec));
    waittime = waittime > 1 ? waittime : 1;
    if (video_show) {
      cv::imshow(window_name, im_mat);
      if ((cv::waitKey(waittime) % 256) == 27)
        break;
      cout << "FPS:" << 1.0 / sec << endl;
    }
  }
}
#endif

void Yolo::PredictYoloDetections(std::vector<JImage *> &images,
                                 std::vector<VecBox> &Bboxes) {
  size_t num_im = images.size();

  int batch = net_.batch_;
  size_t batch_off = num_im % batch;
  size_t batch_num = num_im / batch + (batch_off > 0 ? 1 : 0);

  float *index = nullptr;
  for (int count = 0, b = 1; b <= batch_num; ++b) {
    index = batch_data_;
    int c = 0;
    for (int i = count; i < b * batch && i < num_im; ++i, ++c) {
      if (roi_ != nullptr) {
        images[i]->Crop(im_crop_, *roi_);
        im_crop_->Resize(im_res_, net_.in_h_, net_.in_w_);
      } else {
        images[i]->Resize(im_res_, net_.in_h_, net_.in_w_);
      }
      im_res_->GetBatchData(index);
      index += net_.in_num_;
    }
    predictions_ = net_.PredictNetwork(batch_data_);
    index = predictions_;
    for (int i = 0; i < c; ++i) {
      VecBox boxes(net_.grid_size_ * net_.grid_size_ * net_.box_num_);
      int height = roi_ != nullptr ? im_crop_->h_ : images[count + i]->h_;
      int width = roi_ != nullptr ? im_crop_->w_ : images[count + i]->w_;
      ConvertYoloDetections(index, net_.class_num_, net_.box_num_,
                            net_.sqrt_box_, net_.grid_size_, width, height,
                            boxes);
      Bboxes[count + i] = boxes;
      index += net_.out_num_;
    }
    count += c;
  }
}

void Yolo::ConvertYoloDetections(float *predictions, int classes, int box_num,
                                 int square, int side, int width, int height,
                                 VecBox &boxes) {
  for (int i = 0; i < side * side; ++i) {
    int row = i / side;
    int col = i % side;
    for (int n = 0; n < box_num; ++n) {
      int index = i * box_num + n;
      int p_index = side * side * classes + i * box_num + n;
      float scale = predictions[p_index];
      int box_index = side * side * (classes + box_num) + (i * box_num + n) * 4;

      float x, y, w, h;
      x = (predictions[box_index + 0] + col) / side;
      y = (predictions[box_index + 1] + row) / side;
      w = pow(predictions[box_index + 2], (square ? 2 : 1));
      h = pow(predictions[box_index + 3], (square ? 2 : 1));

      x = constrain(0.f, 1.f, x - w / 2);
      y = constrain(0.f, 1.f, y - h / 2);
      w = constrain(0.f, 1.f, x + w) - x;
      h = constrain(0.f, 1.f, y + h) - y;

      boxes[index].x = x * width;
      boxes[index].y = y * height;
      boxes[index].w = w * width;
      boxes[index].h = h * height;

      float max_score = 0;
      int max_index = -1;
      for (int j = 0; j < classes; ++j) {
        float score = scale * predictions[i * classes + j];
        if (score > threshold_ && score > max_score) {
          max_score = score;
          max_index = j;
        }
      }
      boxes[index].score = max_score;
      boxes[index].class_index = max_index;
    }
  }
}

#ifdef USE_OpenCV
void Yolo::DrawYoloDetections(cv::Mat &im_mat, VecBox &boxes,
                              bool console_show) {
  for (int b = 0; b < boxes.size(); ++b) {
    int classindex = boxes[b].class_index;
    if (classindex == -1)
      continue;

    cv::Scalar scalar;
    if (classindex == 0)
      scalar = cv::Scalar(0, 255, 0);
    else
      scalar = cv::Scalar(255, 0, 0);

    Box box = boxes[b];
    cv::rectangle(im_mat,
                  cv::Point(static_cast<int>(box.x), static_cast<int>(box.y)),
                  cv::Point(static_cast<int>(box.x + box.w),
                            static_cast<int>(box.y + box.h)),
                  scalar, 2, 8, 0);
    if (console_show) {
      std::cout << "x = " << box.x << ", y = " << box.y << ", w = " << box.w
                << ", h = " << box.h << ", score = " << box.score
                << ", label = " << box.class_index << std::endl;
    }
  }
}
#endif

void Yolo::PrintYoloDetections(std::ofstream &file, VecBox &boxes, int count) {
  for (int b = 0; b < boxes.size(); ++b) {
    Box box = boxes[b];
    if (box.class_index == -1)
      continue;
    file << count << ", " << box.x << ", " << box.y << ", " << box.w << ", "
         << box.h << ", " << box.score << endl;
  }
}