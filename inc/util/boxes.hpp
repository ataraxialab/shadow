#ifndef SHADOW_BOXES_HPP
#define SHADOW_BOXES_HPP

#include <vector>

#ifdef USE_OpenCV
#include <opencv2/opencv.hpp>
#endif

struct Box {
  float x, y, w, h, score;
  int class_index;
};

typedef std::vector<Box> VecBox;

class Boxes {
public:
  static float BoxesIoU(const Box &boxA, const Box &boxB);
  static void BoxesNMS(VecBox *boxes, float iou_threshold);
  static void SmoothBoxes(const VecBox &oldBoxes, VecBox *newBoxes,
                          float smooth);
  static void AmendBoxes(VecBox *boxes, Box *roi);

#ifdef USE_OpenCV
  static void SelectRoI(int event, int x, int y, int flags, void *roi);
#endif
};

#endif // SHADOW_BOXES_HPP
