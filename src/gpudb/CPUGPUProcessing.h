#ifndef _CPUGPU_PROCESSING_H_
#define _CPUGPU_PROCESSING_H_

#include "QueryOptimizer.h"
#include "GPUProcessing.h"
#include "CPUProcessing.h"
#include "common.h"

#define OD_BATCH_SIZE 8

class CPUGPUProcessing {
public:
  CacheManager* cm;
  QueryOptimizer* qo;

  bool custom;
  bool skipping;

  int** col_idx;
  // int** od_col_idx;
  chrono::high_resolution_clock::time_point begin_time;
  bool verbose;

  double transfer_time;
  double cpu_time;
  double gpu_time;

  CPUGPUProcessing(size_t _cache_size, size_t _ondemand_size, size_t _processing_size, size_t _pinned_memsize, bool _verbose, bool _custom = true, bool _skipping = true);

  ~CPUGPUProcessing() {
    delete[] col_idx;
    // delete[] od_col_idx;
    delete qo;
  }

  void resetCGP() {
    for (int i = 0; i < cm->TOT_COLUMN; i++) {
      col_idx[i] = NULL;
    }
    // for (int i = 0; i < cm->TOT_COLUMN; i++) {
    //   od_col_idx[i] = NULL;
    // }
  }

  void resetTime() {
    cpu_time = 0;
    gpu_time = 0;
    transfer_time = 0;
  }

  void switch_device_fact(int** &off_col, int** &h_off_col, int* &d_total, int* h_total, int sg, int mode, int table, cudaStream_t stream);

  void call_pfilter_probe_group_by_GPU(QueryParams* params, int** &off_col, int* h_total, int sg, int select_so_far, cudaStream_t stream);

  void call_pfilter_probe_group_by_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg, int select_so_far);

  void call_pfilter_probe_GPU(QueryParams* params, int** &off_col, int* &d_total, int* h_total, int sg, int select_so_far, cudaStream_t stream);

  void call_pfilter_probe_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg, int select_so_far);

  void call_probe_group_by_GPU(QueryParams* params, int** &off_col, int* h_total, int sg, cudaStream_t stream);

  void call_probe_group_by_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg);

  void call_probe_GPU(QueryParams* params, int** &off_col, int* &d_total, int* h_total, int sg, cudaStream_t stream);

  void call_probe_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg);

  void call_pfilter_GPU(QueryParams* params, int** &off_col, int* &d_total, int* h_total, int sg, int select_so_far, cudaStream_t stream);

  void call_pfilter_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg, int select_so_far);



  void switch_device_dim(int* &off_col, int* &h_off_col, int* &d_total, int* h_total, int sg, int mode, int table, cudaStream_t stream);

  void call_bfilter_build_GPU(QueryParams* params, int* &d_off_col, int* h_total, int sg, int table, cudaStream_t stream);

  void call_bfilter_build_CPU(QueryParams* params, int* &h_off_col, int* h_total, int sg, int table);

  void call_build_GPU(QueryParams* params, int* &d_off_col, int* h_total, int sg, int table, cudaStream_t stream);

  void call_build_CPU(QueryParams* params, int* &h_off_col, int* h_total, int sg, int table);

  void call_bfilter_GPU(QueryParams* params, int* &d_off_col, int* &d_total, int* h_total, int sg, int table, cudaStream_t stream);

  void call_bfilter_CPU(QueryParams* params, int* &h_off_col, int* h_total, int sg, int table);



  void call_group_by_GPU(QueryParams* params, int** &off_col, int* h_total, cudaStream_t stream);

  void call_group_by_CPU(QueryParams* params, int** &h_off_col, int* h_total);

  void call_aggregation_GPU(QueryParams* params, int* &off_col, int* h_total, cudaStream_t stream);

  void call_aggregation_CPU(QueryParams* params, int* &h_off_col, int* h_total);

  void call_probe_aggr_GPU(QueryParams* params, int** &off_col, int* h_total, int sg, cudaStream_t stream);

  void call_probe_aggr_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg);

  void call_pfilter_probe_aggr_GPU(QueryParams* params, int** &off_col, int* h_total, int sg, int select_so_far, cudaStream_t stream);

  void call_pfilter_probe_aggr_CPU(QueryParams* params, int** &h_off_col, int* h_total, int sg, int select_so_far);

  void copyColIdx();

  void call_pfilter_probe_aggr_OD(QueryParams* params, 
      ColumnInfo** filter, ColumnInfo** pkey, ColumnInfo** fkey, ColumnInfo** aggr,
      int sg, int batch, int batch_size, int total_batch,
      cudaStream_t stream);

  void call_probe_group_by_OD(QueryParams* params, ColumnInfo** pkey, ColumnInfo** fkey, ColumnInfo** aggr,
    int sg, int batch, int batch_size, int total_batch,
    cudaStream_t stream);

  void call_probe_GPUNP(QueryParams* params, int** &off_col, int* &d_total, int* h_total, int sg, cudaStream_t stream, ColumnInfo* column);

  void call_probe_CPUNP(QueryParams* params, int** &h_off_col, int* h_total, int sg, ColumnInfo* column);

  void call_pfilter_GPUNP(QueryParams* params, int** &off_col, int* &d_total, int* h_total, int sg, cudaStream_t stream, ColumnInfo* column);

  void call_pfilter_CPUNP(QueryParams* params, int** &h_off_col, int* h_total, int sg, ColumnInfo* column);

};

#endif