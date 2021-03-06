#define CL_KERNEL_LOOP(globalid, count)  \
  const int globalid = get_global_id(0); \
  if (globalid >= count) return;

__kernel void DataTransform(__global float *in_data, int count, int in_c,
                            int spatial_dim, float scale, int num_mean,
                            __global float *mean_value,
                            __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int c_out = (globalid / spatial_dim) % in_c;
  int s_out = globalid % spatial_dim;

  if (num_mean == 1) {
    out_data[globalid] = (in_data[globalid] - mean_value[0]) * scale;
  } else if (num_mean == in_c) {
    out_data[globalid] = (in_data[globalid] - mean_value[c_out]) * scale;
  } else if (num_mean == in_c * spatial_dim) {
    out_data[globalid] =
        (in_data[globalid] - mean_value[c_out * spatial_dim + s_out]) * scale;
  }
}

__kernel void Im2Col(__global float *in_data, int offset, int count, int in_c,
                     int in_h, int in_w, int kernel_size, int stride, int pad,
                     int dilation, int zero_point, int out_h, int out_w,
                     __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int h_index = globalid / out_w;
  int h_col = h_index % out_h;
  int w_col = globalid % out_w;
  int c_im = h_index / out_h;
  int c_col = c_im * kernel_size * kernel_size;
  int h_offset = h_col * stride - pad;
  int w_offset = w_col * stride - pad;
  out_data += (c_col * out_h + h_col) * out_w + w_col;
  in_data += offset + (c_im * in_h + h_offset) * in_w + w_offset;
  for (int i = 0; i < kernel_size; ++i) {
    for (int j = 0; j < kernel_size; ++j) {
      int h_im = h_offset + i * dilation;
      int w_im = w_offset + j * dilation;
      *out_data = (h_im >= 0 && w_im >= 0 && h_im < in_h && w_im < in_w)
                      ? in_data[i * dilation * in_w + j * dilation]
                      : zero_point;
      out_data += out_h * out_w;
    }
  }
}

__kernel void Pooling(__global float *in_data, int count, int in_c, int in_h,
                      int in_w, int kernel_size, int stride, int pad, int mode,
                      int out_h, int out_w, __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int temp = globalid / out_w;
  int j_out = globalid % out_w;
  int i_out = temp % out_h;
  temp = temp / out_h;
  int c_out = temp % in_c;
  int b_out = temp / in_c;

  int kistart = i_out * stride - pad, kjstart = j_out * stride - pad;
  int kiend = min(kistart + kernel_size, in_h + pad);
  int kjend = min(kjstart + kernel_size, in_w + pad);
  int pool_size = (kiend - kistart) * (kjend - kjstart);
  kistart = max(kistart, 0), kjstart = max(kjstart, 0);
  kiend = min(kiend, in_h), kjend = min(kjend, in_w);

  float max = -FLT_MAX;
  float sum = 0.f;
  for (int ki = kistart; ki < kiend; ++ki) {
    for (int kj = kjstart; kj < kjend; ++kj) {
      int index = kj + in_w * (ki + in_h * (c_out + in_c * b_out));
      float value = in_data[index];
      max = (value > max) ? value : max;
      sum += value;
    }
  }
  out_data[globalid] = (mode == 0) ? max : sum / pool_size;
}

__kernel void Concat(__global float *in_data, int count, int num_concats,
                     int concat_size, int top_concat_axis,
                     int bottom_concat_axis, int offset_concat_axis,
                     __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int total_concat_size = concat_size * bottom_concat_axis;
  int concat_num = globalid / total_concat_size;
  int concat_index = globalid % total_concat_size;
  int top_index =
      concat_index +
      (concat_num * top_concat_axis + offset_concat_axis) * concat_size;
  out_data[top_index] = in_data[globalid];
}

__kernel void Permute(__global float *in_data, int count, int num_axes,
                      __global int *permute_order, __global int *old_steps,
                      __global int *new_steps, __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int old_idx = 0;
  int idx = globalid;
  for (int j = 0; j < num_axes; ++j) {
    int order = permute_order[j];
    old_idx += (idx / new_steps[j]) * old_steps[order];
    idx %= new_steps[j];
  }
  out_data[globalid] = in_data[old_idx];
}

__kernel void Scale(__global float *in_data, int count,
                    __global float *scale_data, __global float *bias_data,
                    int scale_dim, int inner_dim, __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int index = (globalid / inner_dim) % scale_dim;
  out_data[globalid] = in_data[globalid] * scale_data[index] + bias_data[index];
}

__kernel void Bias(__global float *in_data, int count,
                   __global float *bias_data, int bias_dim, int inner_dim,
                   __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int index = (globalid / inner_dim) % bias_dim;
  out_data[globalid] = in_data[globalid] + bias_data[index];
}

__kernel void Reorg(__global float *in_data, int count, int in_c, int in_h,
                    int in_w, int out_c, int out_h, int out_w, int stride,
                    __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  int temp = globalid / out_w;
  int w = globalid % out_w;
  int h = temp % out_h;
  temp = temp / out_h;
  int c = temp % out_c;
  int b = temp / out_c;

  int c_in = c % in_c;
  int area = c / in_c;
  int h_in = h * stride + area / stride;
  int w_in = w * stride + area % stride;
  int in_index = ((b * in_c + c_in) * in_h + h_in) * in_w + w_in;
  int out_index = ((b * out_c + c) * out_h + h) * out_w + w;
  out_data[out_index] = in_data[in_index];
}

__kernel void LRNFillScale(__global float *in_data, int count, int in_c,
                           int in_h, int in_w, int size, float alpha_over_size,
                           float k, __global float *scale_data) {
  CL_KERNEL_LOOP(globalid, count)

  int temp = globalid / in_w;
  int w = globalid % in_w;
  int h = temp % in_h;
  int b = temp / in_h;

  int offset = (b * in_c * in_h + h) * in_w + w, head = 0;
  __global float *in_off = in_data + offset;
  __global float *scale_off = scale_data + offset;
  float accum_scale = 0;
  int step = in_h * in_w;
  int pre_pad = (size - 1) / 2, post_pad = size - pre_pad - 1;
  while (head < post_pad && head < in_c) {
    accum_scale += in_off[head * step] * in_off[head * step];
    head++;
  }
  while (head < in_c) {
    accum_scale += in_off[head * step] * in_off[head * step];
    if (head - size >= 0) {
      accum_scale -=
          in_off[(head - size) * step] * in_off[(head - size) * step];
    }
    scale_off[(head - post_pad) * step] = k + accum_scale * alpha_over_size;
    head++;
  }
  while (head < in_c + post_pad) {
    if (head - size >= 0) {
      accum_scale -=
          in_off[(head - size) * step] * in_off[(head - size) * step];
    }
    scale_off[(head - post_pad) * step] = k + accum_scale * alpha_over_size;
    head++;
  }
}

__kernel void LRN(__global float *in_data, int count,
                  __global float *scale_data, float negative_beta,
                  __global float *out_data) {
  CL_KERNEL_LOOP(globalid, count)

  out_data[globalid] =
      in_data[globalid] * pow(scale_data[globalid], negative_beta);
}

inline float ActivateValue(float x, int type, float slope) {
  // PRelu: 0, Relu: 1, Leaky: 2, Sigmoid: 3, SoftPlus: 4, Tanh: 5
  switch (type) {
    case 1:
      return x * (x > 0);
    case 2:
      return x > 0 ? x : slope * x;
    case 3:
      return 1 / (1 + exp(-x));
    case 4:
      return log(1 + exp(x));
    case 5: {
      float exp_2x = exp(2 * x);
      return (exp_2x - 1) / (exp_2x + 1);
    }
    default:
      return x;
  }
}

__kernel void Activate(__global float *data, int count, int type, float slope) {
  CL_KERNEL_LOOP(globalid, count)

  data[globalid] = ActivateValue(data[globalid], type, slope);
}

__kernel void PRelu(__global float *data, int count, int channels, int dim,
                    int div_factor, __global float *slope_data) {
  CL_KERNEL_LOOP(globalid, count)

  int c = (globalid / dim) % channels / div_factor;
  float value = data[globalid];
  data[globalid] = value > 0 ? value : value * slope_data[c];
}
