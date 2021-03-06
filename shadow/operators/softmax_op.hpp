#ifndef SHADOW_OPERATORS_SOFTMAX_OP_HPP
#define SHADOW_OPERATORS_SOFTMAX_OP_HPP

#include "core/operator.hpp"

namespace Shadow {

class SoftmaxOp : public Operator {
 public:
  explicit SoftmaxOp(const shadow::OpParam &op_param, Workspace *ws)
      : Operator(op_param, ws) {}
  ~SoftmaxOp() override { Release(); }

  void Setup() override;
  void Reshape() override;
  void Forward() override;
  void Release() override;

 private:
  int axis_, outer_num_, inner_num_;

  BlobF *scale_ = nullptr;

#if defined(USE_CUDNN)
  cudnnTensorDescriptor_t bottom_desc_ = nullptr, top_desc_ = nullptr;
#endif
};

}  // namespace Shadow

#endif  // SHADOW_OPERATORS_SOFTMAX_OP_HPP
