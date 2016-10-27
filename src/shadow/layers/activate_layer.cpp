#include "shadow/layers/activate_layer.hpp"
#include "shadow/util/blas.hpp"
#include "shadow/util/image.hpp"

void ActivateLayer::Setup(VecBlob *blobs) {
  Layer::Setup(blobs);

  const shadow::ActivateParameter &activate_param =
      layer_param_.activate_param();

  activate_type_ = layer_param_.activate_param().type();
}

void ActivateLayer::Reshape() {
  if (bottoms_[0] != tops_[0]) {
    tops_[0]->reshape(bottoms_[0]->shape());
  }

  std::stringstream out;
  out << layer_name_ << ": "
      << Util::format_vector(bottoms_[0]->shape(), ",", "(", ")") << " -> "
      << Util::format_vector(tops_[0]->shape(), ",", "(", ")");
  DInfo(out.str());
}

void ActivateLayer::Forward() {
  if (bottoms_[0] != tops_[0]) {
    Blas::BlasScopy(bottoms_[0]->count(), bottoms_[0]->data(), 0,
                    tops_[0]->mutable_data(), 0);
  }
  Image::Activate(tops_[0]->mutable_data(), tops_[0]->count(), activate_type_);
}

void ActivateLayer::Release() {
  bottoms_.clear();
  tops_.clear();

  // DInfo("Free ActivateLayer!");
}