#ifndef SHADOW_BLOB_HPP
#define SHADOW_BLOB_HPP

#include "shadow/kernel.hpp"
#include "shadow/util/util.hpp"

#include "shadow/proto/shadow.pb.h"

template <class Dtype> class BaseBlob {
public:
  BaseBlob() {}
  explicit BaseBlob(std::string name) { name_ = name; }

  inline const Dtype *data() const { return data_; }
  inline Dtype *mutable_data() { return data_; }

  inline void set_data(float *data) {
#if defined(USE_CUDA)
    CUDA::CUDAWriteBuffer(count(), data_, data);
    on_gpu_ = true;
#elif defined(USE_CL)
    CL::CLWriteBuffer(count(), data_, data);
    on_gpu_ = true;
#else
    memcpy(data_, data, sizeof(float) * count());
    on_gpu_ = false;
#endif
  }

  inline void allocate_data(int count) {
#if defined(USE_CUDA)
    data_ = CUDA::CUDAMakeBuffer(count, NULL);
    on_gpu_ = true;
#elif defined(USE_CL)
    data_ = new cl_mem();
    *data_ = CL::CLMakeBuffer(count, CL_MEM_READ_WRITE, nullptr);
    on_gpu_ = true;
#else
    data_ = new float[count];
    on_gpu_ = false;
#endif
    if (shape_.size() == 0)
      add_shape(count);
  }

  inline void copy_data(float *out_data) const {
    if (on_gpu_) {
#if defined(USE_CUDA)
      CUDA::CUDAReadBuffer(count(), data_, out_data);
#elif defined(USE_CL)
      CL::CLReadBuffer(count(), data_, out_data);
#endif
    } else {
      memcpy(out_data, data_, sizeof(float) * count());
    }
  }

  inline const std::string name() const { return name_; }
  inline void set_name(std::string name) { name_ = name; }

  inline const std::vector<int> shape() const { return shape_; }
  inline std::vector<int> *mutable_shape() { return &shape_; }

  inline const int shape(int index) const {
    if (index < 0 || index >= shape_.size())
      Fatal("Index out of blob shape range!");
    return shape_[index];
  }
  inline void set_shape(int index, int value) {
    if (index < 0 || index >= shape_.size())
      Fatal("Index out of blob shape range!");
    shape_[index] = value;
  }
  inline void set_shape(shadow::BlobShape shape) {
    shape_.clear();
    for (int i = 0; i < shape.dim_size(); ++i) {
      shape_.push_back(shape.dim(i));
    }
  }
  inline void add_shape(int value) { shape_.push_back(value); }

  inline const int num() const { return count() / shape(0); }
  inline const int count() const {
    int count = 1;
    for (int i = 0; i < shape_.size(); ++i)
      count *= shape(i);
    return count;
  }

  inline void clear() {
    if (data_ != nullptr) {
#if defined(USE_CUDA)
      CUDA::CUDAReleaseBuffer(data_);
#elif defined(USE_CL)
      CL::CLReleaseBuffer(data_);
#else
      delete[] data_;
#endif
    }
    shape_.clear();
  }

private:
  Dtype *data_;

  std::string name_;
  std::vector<int> shape_;
  bool on_gpu_;
};

typedef BaseBlob<BType> Blob;
typedef std::vector<Blob *> VecBlob;

inline static Blob *find_blob_by_name(const VecBlob &blobs, std::string name) {
  for (int i = 0; i < blobs.size(); ++i) {
    if (!name.compare(blobs.at(i)->name()))
      return blobs.at(i);
  }
  return nullptr;
}

#endif // SHADOW_BLOB_HPP
