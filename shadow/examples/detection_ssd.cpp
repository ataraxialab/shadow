#include "detection_ssd.hpp"

namespace Shadow {

inline void ClipBBox(const BoxF &bbox, BoxF *clip_bbox) {
  clip_bbox->xmin = std::max(std::min(bbox.xmin, 1.f), 0.f);
  clip_bbox->ymin = std::max(std::min(bbox.ymin, 1.f), 0.f);
  clip_bbox->xmax = std::max(std::min(bbox.xmax, 1.f), 0.f);
  clip_bbox->ymax = std::max(std::min(bbox.ymax, 1.f), 0.f);
}

template <typename T>
inline bool SortScorePairDescend(const std::pair<float, T> &pair1,
                                 const std::pair<float, T> &pair2) {
  return pair1.first > pair2.first;
}

inline void GetMaxScoreIndex(
    const VecFloat &scores, float threshold, int top_k,
    std::vector<std::pair<float, int>> *score_index_vec) {
  // Generate index score pairs.
  for (int i = 0; i < scores.size(); ++i) {
    if (scores[i] > threshold) {
      score_index_vec->push_back(std::make_pair(scores[i], i));
    }
  }

  // Sort the score pair according to the scores in descending order
  std::stable_sort(score_index_vec->begin(), score_index_vec->end(),
                   SortScorePairDescend<int>);

  // Keep top_k scores if needed.
  if (top_k > -1 && top_k < score_index_vec->size()) {
    score_index_vec->resize(top_k);
  }
}

inline void ApplyNMSFast(const VecBoxF &bboxes, VecFloat &scores,
                         float score_threshold, float nms_threshold, int top_k,
                         VecInt *indices) {
  // Sanity check.
  CHECK_EQ(bboxes.size(), scores.size());

  // Get top_k scores (with corresponding indices).
  std::vector<std::pair<float, int>> score_index_vec;
  GetMaxScoreIndex(scores, score_threshold, top_k, &score_index_vec);

  // Do nms.
  indices->clear();
  while (!score_index_vec.empty()) {
    std::stable_sort(score_index_vec.begin(), score_index_vec.end(),
                     SortScorePairDescend<int>);
    const int idx = score_index_vec.front().second;
    for (int i = 1; i < score_index_vec.size(); ++i) {
      float overlap = Boxes::IoU(bboxes[idx], bboxes[i]);
      float scale = std::exp(-overlap * overlap / 0.5f);
      score_index_vec[i].first *= scale;
      scores[score_index_vec[i].second] *= scale;
    }
    indices->push_back(idx);
    score_index_vec.erase(score_index_vec.begin());
  }
}

void DetectionSSD::Setup(const std::string &model_file, const VecInt &classes,
                         const VecInt &in_shape) {
  net_.Setup();

  net_.LoadModel(model_file, in_shape);

  batch_ = net_.in_shape()[0];
  in_c_ = net_.in_shape()[1];
  in_h_ = net_.in_shape()[2];
  in_w_ = net_.in_shape()[3];
  in_num_ = in_c_ * in_h_ * in_w_;

  in_data_.resize(batch_ * in_num_);

  threshold_ = 0.6;
  num_classes_ = classes[0];
  num_priors_ = net_.GetBlobByName<float>("mbox_priorbox")->shape(2) / 4;
  num_loc_classes_ = 1;
  background_label_id_ = 0;
  top_k_ = 400;
  keep_top_k_ = 200;
  nms_threshold_ = 0.45;
  confidence_threshold_ = 0.01;
  share_location_ = true;
}

void DetectionSSD::Predict(const JImage &im_src, const VecRectF &rois,
                           std::vector<VecBoxF> *Bboxes) {
  CHECK_LE(rois.size(), batch_);
  for (int b = 0; b < rois.size(); ++b) {
    ConvertData(im_src, in_data_.data() + b * in_num_, rois[b], in_c_, in_h_,
                in_w_);
  }

  Process(in_data_.data(), Bboxes);

  CHECK_EQ(Bboxes->size(), rois.size());
  for (int b = 0; b < Bboxes->size(); ++b) {
    float height = rois[b].h, width = rois[b].w;
    auto &boxes = Bboxes->at(b);
    for (auto &box : boxes) {
      box.xmin *= width;
      box.xmax *= width;
      box.ymin *= height;
      box.ymax *= height;
    }
  }
}

#if defined(USE_OpenCV)
void DetectionSSD::Predict(const cv::Mat &im_mat, const VecRectF &rois,
                           std::vector<VecBoxF> *Bboxes) {
  im_ini_.FromMat(im_mat, true);
  Predict(im_ini_, rois, Bboxes);
}
#endif

void DetectionSSD::Release() {
  net_.Release();

  in_data_.clear();
}

void DetectionSSD::Process(const float *data, std::vector<VecBoxF> *Bboxes) {
  net_.Forward(data);

  const auto *loc_data = net_.GetBlobDataByName<float>("mbox_loc");
  const auto *conf_data = net_.GetBlobDataByName<float>("mbox_conf_flatten");
  const auto *prior_data = net_.GetBlobDataByName<float>("mbox_priorbox");

  VecLabelBBox all_loc_preds;
  GetLocPredictions(loc_data, batch_, num_priors_, num_loc_classes_,
                    share_location_, &all_loc_preds);

  std::vector<std::map<int, VecFloat>> all_conf_scores;
  GetConfidenceScores(conf_data, batch_, num_priors_, num_classes_,
                      &all_conf_scores);

  VecBoxF prior_bboxes;
  std::vector<VecFloat> prior_variances;
  GetPriorBBoxes(prior_data, num_priors_, &prior_bboxes, &prior_variances);

  VecLabelBBox all_decode_bboxes;
  DecodeBBoxesAll(all_loc_preds, prior_bboxes, prior_variances, batch_,
                  share_location_, num_loc_classes_, background_label_id_,
                  &all_decode_bboxes);

  int num_kept = 0;
  std::vector<std::map<int, VecInt>> all_indices;
  for (int b = 0; b < batch_; ++b) {
    auto &conf_scores = all_conf_scores[b];
    const auto &decode_bboxes = all_decode_bboxes[b];
    std::map<int, VecInt> indices;
    int num_det = 0;
    for (int c = 0; c < num_classes_; ++c) {
      if (c == background_label_id_) {
        continue;
      }
      if (conf_scores.find(c) == conf_scores.end()) {
        LOG(FATAL) << "Could not find confidence predictions";
      }
      auto &scores = conf_scores.find(c)->second;
      int label = share_location_ ? -1 : c;
      if (decode_bboxes.find(label) == decode_bboxes.end()) {
        LOG(FATAL) << "Could not find confidence predictions";
      }
      const auto &bboxes = decode_bboxes.find(label)->second;
      ApplyNMSFast(bboxes, scores, confidence_threshold_, nms_threshold_,
                   top_k_, &(indices[c]));
      num_det += indices[c].size();
    }
    if (keep_top_k_ > -1 && num_det > keep_top_k_) {
      std::vector<std::pair<float, std::pair<int, int>>> score_index_pairs;
      for (const auto &it : indices) {
        int label = it.first;
        if (conf_scores.find(label) == conf_scores.end()) {
          LOG(FATAL) << "Could not find confidence predictions";
        }
        const auto &scores = conf_scores.find(label)->second;
        for (const auto &idx : it.second) {
          CHECK_LT(idx, scores.size());
          score_index_pairs.emplace_back(scores[idx],
                                         std::make_pair(label, idx));
        }
      }
      // Keep top k results per image.
      std::sort(score_index_pairs.begin(), score_index_pairs.end(),
                SortScorePairDescend<std::pair<int, int>>);
      score_index_pairs.resize(keep_top_k_);
      // Store the new indices.
      std::map<int, VecInt> new_indices;
      for (const auto &score_index_pair : score_index_pairs) {
        int label = score_index_pair.second.first;
        int idx = score_index_pair.second.second;
        new_indices[label].push_back(idx);
      }
      all_indices.push_back(new_indices);
      num_kept += keep_top_k_;
    } else {
      all_indices.push_back(indices);
      num_kept += num_det;
    }
  }

  Bboxes->clear();
  for (int b = 0; b < batch_; ++b) {
    VecBoxF boxes;
    const auto &conf_scores = all_conf_scores[b];
    const auto &decode_bboxes = all_decode_bboxes[b];
    for (const auto &it : all_indices[b]) {
      int label = it.first;
      if (conf_scores.find(label) == conf_scores.end()) {
        LOG(FATAL) << "Could not find confidence predictions";
      }
      const auto &scores = conf_scores.find(label)->second;
      int loc_label = share_location_ ? -1 : label;
      if (decode_bboxes.find(loc_label) == decode_bboxes.end()) {
        LOG(FATAL) << "Could not find confidence predictions";
      }
      const auto &bboxes = decode_bboxes.find(loc_label)->second;
      for (const auto &idx : it.second) {
        float score = scores[idx];
        if (score > threshold_) {
          BoxF clip_bbox;
          ClipBBox(bboxes[idx], &clip_bbox);
          clip_bbox.score = score;
          clip_bbox.label = label;
          boxes.push_back(clip_bbox);
        }
      }
    }
    Bboxes->push_back(boxes);
  }
}

void DetectionSSD::GetLocPredictions(const float *loc_data, int num,
                                     int num_preds_per_class,
                                     int num_loc_classes, bool share_location,
                                     VecLabelBBox *loc_preds) {
  loc_preds->resize(num);
  for (auto &label_bbox : *loc_preds) {
    for (int p = 0; p < num_preds_per_class; ++p) {
      int start_idx = p * num_loc_classes * 4;
      for (int c = 0; c < num_loc_classes; ++c) {
        int label = share_location ? -1 : c;
        if (label_bbox.find(label) == label_bbox.end()) {
          label_bbox[label].resize(num_preds_per_class);
        }
        label_bbox[label][p].xmin = loc_data[start_idx + c * 4];
        label_bbox[label][p].ymin = loc_data[start_idx + c * 4 + 1];
        label_bbox[label][p].xmax = loc_data[start_idx + c * 4 + 2];
        label_bbox[label][p].ymax = loc_data[start_idx + c * 4 + 3];
      }
    }
    loc_data += num_preds_per_class * num_loc_classes * 4;
  }
}

void DetectionSSD::GetConfidenceScores(
    const float *conf_data, int num, int num_preds_per_class, int num_classes,
    std::vector<std::map<int, VecFloat>> *conf_preds) {
  conf_preds->clear();
  conf_preds->resize(num);
  for (auto &label_scores : *conf_preds) {
    for (int p = 0; p < num_preds_per_class; ++p) {
      int start_idx = p * num_classes;
      for (int c = 0; c < num_classes; ++c) {
        label_scores[c].push_back(conf_data[start_idx + c]);
      }
    }
    conf_data += num_preds_per_class * num_classes;
  }
}

void DetectionSSD::GetPriorBBoxes(const float *prior_data, int num_priors,
                                  VecBoxF *prior_bboxes,
                                  std::vector<VecFloat> *prior_variances) {
  prior_bboxes->clear();
  prior_variances->clear();
  for (int i = 0; i < num_priors; ++i) {
    int start_idx = i * 4;
    BoxF bbox;
    bbox.xmin = prior_data[start_idx];
    bbox.ymin = prior_data[start_idx + 1];
    bbox.xmax = prior_data[start_idx + 2];
    bbox.ymax = prior_data[start_idx + 3];
    prior_bboxes->push_back(bbox);
  }

  for (int i = 0; i < num_priors; ++i) {
    int start_idx = (num_priors + i) * 4;
    VecFloat var;
    for (int j = 0; j < 4; ++j) {
      var.push_back(prior_data[start_idx + j]);
    }
    prior_variances->push_back(var);
  }
}

void DetectionSSD::DecodeBBoxesAll(const VecLabelBBox &all_loc_preds,
                                   const VecBoxF &prior_bboxes,
                                   const std::vector<VecFloat> &prior_variances,
                                   int num, bool share_location,
                                   int num_loc_classes, int background_label_id,
                                   VecLabelBBox *all_decode_bboxes) {
  CHECK_EQ(all_loc_preds.size(), num);
  all_decode_bboxes->clear();
  all_decode_bboxes->resize(num);
  for (int i = 0; i < num; ++i) {
    auto &decode_bboxes = all_decode_bboxes->at(i);
    for (int c = 0; c < num_loc_classes; ++c) {
      int label = share_location ? -1 : c;
      if (label == background_label_id) {
        continue;
      }
      if (all_loc_preds[i].find(label) == all_loc_preds[i].end()) {
        LOG(FATAL) << "Could not find location predictions";
      }
      const auto &label_loc_preds = all_loc_preds[i].find(label)->second;
      DecodeBBoxes(prior_bboxes, prior_variances, label_loc_preds,
                   &(decode_bboxes[label]));
    }
  }
}

void DetectionSSD::DecodeBBoxes(const VecBoxF &prior_bboxes,
                                const std::vector<VecFloat> &prior_variances,
                                const VecBoxF &bboxes, VecBoxF *decode_bboxes) {
  CHECK_EQ(prior_bboxes.size(), prior_variances.size());
  CHECK_EQ(prior_bboxes.size(), bboxes.size());
  size_t num_bboxes = prior_bboxes.size();
  if (num_bboxes >= 1) {
    CHECK_EQ(prior_variances[0].size(), 4);
  }
  decode_bboxes->clear();
  for (int i = 0; i < num_bboxes; ++i) {
    BoxF decode_bbox;
    DecodeBBox(prior_bboxes[i], prior_variances[i], bboxes[i], &decode_bbox);
    decode_bboxes->push_back(decode_bbox);
  }
}

void DetectionSSD::DecodeBBox(const BoxF &prior_bbox,
                              const VecFloat &prior_variance, const BoxF &bbox,
                              BoxF *decode_bbox) {
  float prior_width = prior_bbox.xmax - prior_bbox.xmin;
  CHECK_GT(prior_width, 0);
  float prior_height = prior_bbox.ymax - prior_bbox.ymin;
  CHECK_GT(prior_height, 0);
  float prior_center_x = (prior_bbox.xmin + prior_bbox.xmax) / 2.f;
  float prior_center_y = (prior_bbox.ymin + prior_bbox.ymax) / 2.f;

  float decode_bbox_center_x, decode_bbox_center_y;
  float decode_bbox_width, decode_bbox_height;

  // variance is encoded in bbox, we need to scale the offset accordingly.
  decode_bbox_center_x =
      prior_variance[0] * bbox.xmin * prior_width + prior_center_x;
  decode_bbox_center_y =
      prior_variance[1] * bbox.ymin * prior_height + prior_center_y;
  decode_bbox_width = std::exp(prior_variance[2] * bbox.xmax) * prior_width;
  decode_bbox_height = std::exp(prior_variance[3] * bbox.ymax) * prior_height;

  decode_bbox->xmin = decode_bbox_center_x - decode_bbox_width / 2.f;
  decode_bbox->ymin = decode_bbox_center_y - decode_bbox_height / 2.f;
  decode_bbox->xmax = decode_bbox_center_x + decode_bbox_width / 2.f;
  decode_bbox->ymax = decode_bbox_center_y + decode_bbox_height / 2.f;
}

}  // namespace Shadow
