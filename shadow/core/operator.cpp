#include "operator.hpp"

namespace Shadow {

Operator::Operator(const shadow::OpParam &op_param, Workspace *ws)
    : op_param_(op_param), arg_helper_(op_param_), op_ws_(ws) {
  op_name_ = op_param_.name();
  op_type_ = op_param_.type();
  bottoms_.clear(), tops_.clear(), blobs_.clear();
  for (const auto &bottom_name : op_param_.bottom()) {
    auto *bottom = ws->GetBlob<float>(bottom_name);
    if (bottom != nullptr) {
      if (bottom->num()) {
        bottoms_.push_back(bottom);
      } else {
        LOG(FATAL) << op_name_ << ": bottom blob(" << bottom_name
                   << Util::format_vector(bottom->shape(), ",", "(", ")")
                   << ") dimension mismatch!";
      }
    } else {
      LOG(FATAL) << op_name_ << ": bottom blob(" << bottom_name
                 << ") not exist!";
    }
  }
  for (const auto &top_name : op_param_.top()) {
    auto *top = ws->CreateBlob<float>(top_name);
    tops_.push_back(top);
  }
  int blob_count = 0;
  for (const auto &proto_blob : op_param_.blobs()) {
    const auto &dims = proto_blob.shape();
    VecInt shape;
    int cc = 1, data_size = proto_blob.data_size();
    for (const auto dim : dims) {
      cc *= dim;
      shape.push_back(dim);
    }
    const auto &blob_name =
        op_name_ + "_" + op_type_ + "_params_" + Util::to_string(blob_count++);
    auto *blob = ws->CreateBlob<float>(shape, blob_name, true);
    if (data_size > 0) {
      CHECK_EQ(data_size, cc) << "Blob data size and blob shape are mismatch";
      blob->set_data(proto_blob.data().data(), data_size);
    }
    blobs_.push_back(blob);
  }
}

Operator::~Operator() {
  op_param_.Clear();
  bottoms_.clear();
  tops_.clear();
  blobs_.clear();
}

Operator *CreateOperator(const shadow::OpParam &op_param, Workspace *ws) {
  static StaticLinkingProtector g_protector;
  auto *registry = OperatorRegistry();
  return registry->Create(op_param.type(), op_param, ws);
}

SHADOW_DEFINE_REGISTRY(OperatorRegistry, Operator, const shadow::OpParam &,
                       Workspace *);

}  // namespace Shadow
