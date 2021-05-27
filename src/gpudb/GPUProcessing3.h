#include <cub/cub.cuh>
#include <curand.h>

#include <cuda.h>
#include <cub/util_allocator.cuh>

using namespace cub;

#define HASH_WM(X,Y,Z) ((X-Z) % Y)

#define CUB_STDERR

#define CHECK_ERROR() { \
  cudaDeviceSynchronize(); \
  cudaError_t error = cudaGetLastError(); \
  if(error != cudaSuccess) \
  { \
    printf("CUDA error: %s\n", cudaGetErrorString(error)); \
    exit(-1); \
  } \
}

#define gpuErrchk(ans) { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort=true)
{
   if (code != cudaSuccess) 
   {
      fprintf(stderr,"GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
      if (abort) exit(code);
   }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGPU(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&offset)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int* ht,
    int ht_len,
    int keys_min,
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    // Out-of-bounds items are selection_flags
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      int hash = HASH(items[ITEM], ht_len, keys_min);
      if (selection_flags[ITEM]) {  
        int slot = ht[(hash << 1) + 1];
        if (slot != 0) {
          offset[ITEM] = slot - 1;
        } else {
          selection_flags[ITEM] = 0;
        }
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGPU2(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&items_lo)[ITEMS_PER_THREAD],
    int  (&offset)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int* gpuCache,
    int* dimkey_idx,
    int* lo_off,
    int* ht,
    int ht_len,
    int keys_min,
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      if (lo_off != NULL) {
        int dimkey_seg = dimkey_idx[items_lo[ITEM] / SEGMENT_SIZE];
        items[ITEM] = gpuCache[dimkey_seg * SEGMENT_SIZE + (items_lo[ITEM] % SEGMENT_SIZE)];
      }

      // Out-of-bounds items are selection_flags
      int hash = HASH(items[ITEM], ht_len, keys_min);
      if (selection_flags[ITEM]) {
        int slot = ht[(hash << 1) + 1];
        if (slot != 0) {
          offset[ITEM] = slot - 1;
        } else {
          selection_flags[ITEM] = 0;
        }
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGroupByGPU(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&res)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int* ht,
    int ht_len,
    int keys_min,
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    // Out-of-bounds items are selection_flags
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      int hash = HASH_WM(items[ITEM], ht_len, keys_min);
      if (selection_flags[ITEM]) {
        uint64_t slot = *reinterpret_cast<uint64_t*>(&ht[hash << 1]);
        if (slot != 0) {
          res[ITEM] = (slot >> 32);
        } else {
          selection_flags[ITEM] = 0;
        }
      }
    }
  }
}


template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGroupByGPU2(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&items_lo)[ITEMS_PER_THREAD],
    int  (&res)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int* gpuCache,
    int* dimkey_idx,
    int* lo_off,
    int* ht,
    int ht_len,
    int keys_min,
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      if (lo_off != NULL) {
        int dimkey_seg = dimkey_idx[items_lo[ITEM] / SEGMENT_SIZE];
        items[ITEM] = gpuCache[dimkey_seg * SEGMENT_SIZE + (items_lo[ITEM] % SEGMENT_SIZE)];
      }

      // Out-of-bounds items are selection_flags
      int hash = HASH_WM(items[ITEM], ht_len, keys_min);

      if (selection_flags[ITEM]) {
        uint64_t slot = *reinterpret_cast<uint64_t*>(&ht[hash << 1]);
        if (slot != 0) {
          res[ITEM] = (slot >> 32);
          //printf("groupval1 = %d\n", groupval1[ITEM]);
        } else {
          selection_flags[ITEM] = 0;
        }
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGPUHelper(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&offset)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      if (selection_flags[ITEM]) {
        offset[ITEM] = items[ITEM];
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGroupByGPUHelper(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&res)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      if (selection_flags[ITEM]) {
        res[ITEM] = 0;
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__device__ __forceinline__ void BlockProbeGroupByGPUHelper2(
    int tid,
    int  (&items)[ITEMS_PER_THREAD],
    int  (&res)[ITEMS_PER_THREAD],
    int  (&selection_flags)[ITEMS_PER_THREAD],
    int* gpuCache,
    int* dimkey_idx,
    int num_items
    ) {
  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    if ((tid * ITEMS_PER_THREAD) + ITEM < num_items) {
      if (selection_flags[ITEM]) {
        int dim_seg = dimkey_idx[items[ITEM] / SEGMENT_SIZE];
        res[ITEM] = gpuCache[dim_seg * SEGMENT_SIZE + (items[ITEM] % SEGMENT_SIZE)];
      }
    }
  }
}



template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__global__ void probe_GPU(int* dim_key1, int* dim_key2, int* dim_key3, int* dim_key4,
  int fact_len, int* ht1, int dim_len1, int* ht2, int dim_len2, int* ht3, int dim_len3, int* ht4, int dim_len4,
  int min_key1, int min_key2, int min_key3, int min_key4,
  int* lo_off, int* dim_off1, int* dim_off2, int* dim_off3, int* dim_off4, 
  int *total, int start_offset) {

  // Specialize BlockLoad for a 1D block of 128 threads owning 4 integer items each
  typedef cub::BlockLoad<int, BLOCK_THREADS, ITEMS_PER_THREAD, BLOCK_LOAD_TRANSPOSE> BlockLoadInt;
  typedef cub::BlockScan<int, BLOCK_THREADS> BlockScanInt;
  int tile_size = BLOCK_THREADS * ITEMS_PER_THREAD;
  int tile_idx = blockIdx.x;    // Current tile index
  int tile_offset = tile_idx * tile_size;

  // Allocate shared memory for BlockLoad
  __shared__ union TempStorage
  {
    typename BlockLoadInt::TempStorage load_items;
    typename BlockScanInt::TempStorage scan;
  } temp_storage;

  // Load a segment of consecutive items that are blocked across threads
  int items[ITEMS_PER_THREAD];
  int selection_flags[ITEMS_PER_THREAD];
  int dim_offset1[ITEMS_PER_THREAD];
  int dim_offset2[ITEMS_PER_THREAD];
  int dim_offset3[ITEMS_PER_THREAD];
  int dim_offset4[ITEMS_PER_THREAD];
  int t_count = 0; // Number of items selected per thread
  int c_t_count = 0; //Prefix sum of t_count
  __shared__ int block_off;

  int num_tiles = (fact_len + tile_size - 1) / tile_size;
  int num_tile_items = tile_size;
  bool is_last_tile = false;
  if (tile_idx == num_tiles - 1) {
    num_tile_items = fact_len - tile_offset;
    is_last_tile = true;
  }

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    selection_flags[ITEM] = 1;
  }

  __syncthreads();

  if (dim_key1 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key1 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();


    BlockProbeGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset1, selection_flags, ht1, dim_len1, min_key1, num_tile_items);
  }

  __syncthreads();

  if (dim_key2 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key2 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset2, selection_flags, ht2, dim_len2, min_key2, num_tile_items);
  }

  __syncthreads();

  if (dim_key3 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key3 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset3, selection_flags, ht3, dim_len3, min_key3, num_tile_items);
  }

  __syncthreads();

  if (dim_key4 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key4 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    /*
     * Join with date table.
     */
    #pragma unroll
    for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
    {
      if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
        int hash = HASH(items[ITEM], dim_len4, min_key4); //19920101
        if (selection_flags[ITEM]) {
          int slot = ht4[(hash << 1) + 1];
          if (slot != 0) {
            t_count++;
            dim_offset4[ITEM] = slot - 1;
          } else {
            selection_flags[ITEM] = 0;
          }
        }
      }
    }
  } else {
    #pragma unroll
    for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
    {
      if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
        if (selection_flags[ITEM]) {
          t_count++;
        }
      }
    }
  }

  //Barrier
  __syncthreads();

  // TODO: need to check logic for offset
  BlockScanInt(temp_storage.scan).ExclusiveSum(t_count, c_t_count); //doing a prefix sum of all the previous threads in the block and store it to c_t_count
  if(threadIdx.x == blockDim.x - 1) { //if the last thread in the block, add the prefix sum of all the prev threads + sum of my threads to global variable total
      block_off = atomicAdd(total, t_count+c_t_count); //the previous value of total is gonna be assigned to block_off
  } //block_off does not need to be global (it's just need to be shared), because it will get the previous value from total which is global

  __syncthreads();

  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM) {
    if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
      if(selection_flags[ITEM]) {
        int offset = block_off + c_t_count++;
        lo_off[offset] = start_offset + blockIdx.x * tile_size + threadIdx.x * ITEMS_PER_THREAD + ITEM;
        if (dim_off1 != NULL) dim_off1[offset] = dim_offset1[ITEM];
        if (dim_off2 != NULL) dim_off2[offset] = dim_offset2[ITEM];
        if (dim_off3 != NULL) dim_off3[offset] = dim_offset3[ITEM];
        if (dim_off4 != NULL) dim_off4[offset] = dim_offset4[ITEM];
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__global__ void probe_GPU2(
  int* lo_off, int* dim_off1, int* dim_off2, int* dim_off3, int* dim_off4,
  int* gpuCache, int* dimkey_idx1, int* dimkey_idx2, int* dimkey_idx3, int* dimkey_idx4,
  int fact_len, int* ht1, int dim_len1, int* ht2, int dim_len2, int* ht3, int dim_len3, int* ht4, int dim_len4,
  int min_key1, int min_key2, int min_key3, int min_key4,
  int* out_lo_off, int* out_dim_off1, int* out_dim_off2, int* out_dim_off3, int* out_dim_off4, 
  int *total, int start_offset, int* segment_group) {

  //assume start_offset always in the beginning of a segment (ga mungkin start di tengah2 segment)
  //assume tile_size is a factor of SEGMENT_SIZE (SEGMENT SIZE kelipatan tile_size)

  // Specialize BlockLoad for a 1D block of 128 threads owning 4 integer items each
  typedef cub::BlockLoad<int, BLOCK_THREADS, ITEMS_PER_THREAD, BLOCK_LOAD_TRANSPOSE> BlockLoadInt;
  typedef cub::BlockScan<int, BLOCK_THREADS> BlockScanInt;
  int tile_size = BLOCK_THREADS * ITEMS_PER_THREAD;
  int tile_idx = blockIdx.x;    // Current tile index
  int tile_offset = blockIdx.x * tile_size;

  int tiles_per_segment = SEGMENT_SIZE/tile_size;
  int segment_index;
  if (segment_group == NULL)
    segment_index = ( start_offset + tile_offset ) / SEGMENT_SIZE;
  else {
    int idx = tile_offset / SEGMENT_SIZE;
    segment_index = segment_group[idx];
    start_offset = segment_index * SEGMENT_SIZE;
    tile_idx = blockIdx.x % tiles_per_segment;
  }
  int segment_tile_offset = (blockIdx.x % tiles_per_segment) * tile_size; //tile offset inside a segment

  // Allocate shared memory for BlockLoad
  __shared__ union TempStorage
  {
    typename BlockLoadInt::TempStorage load_items;
    typename BlockScanInt::TempStorage scan;
  } temp_storage;

  // Load a segment of consecutive items that are blocked across threads
  int items[ITEMS_PER_THREAD];
  int items_lo[ITEMS_PER_THREAD];
  int selection_flags[ITEMS_PER_THREAD];
  int dim_offset1[ITEMS_PER_THREAD];
  int dim_offset2[ITEMS_PER_THREAD];
  int dim_offset3[ITEMS_PER_THREAD];
  int dim_offset4[ITEMS_PER_THREAD];
  int t_count = 0; // Number of items selected per thread
  int c_t_count = 0; //Prefix sum of t_count
  __shared__ int block_off;

  int num_tiles = (fact_len + tile_size - 1) / tile_size;
  int num_tile_items = tile_size;
  bool is_last_tile = false;
  if (tile_idx == num_tiles - 1) {
    num_tile_items = fact_len - tile_offset;
    is_last_tile = true;
  }

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    selection_flags[ITEM] = 1;
  }

  __syncthreads();

  if (lo_off != NULL) BlockLoadInt(temp_storage.load_items).Load(lo_off + tile_offset, items_lo, num_tile_items);
  // Barrier for smem reuse
  __syncthreads();

  if (dimkey_idx1 != NULL && dim_off1 == NULL) {

    if (lo_off == NULL) {
      int dimkey_seg1 = dimkey_idx1[segment_index];
      int* ptr = gpuCache + dimkey_seg1 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, dim_offset1, selection_flags, gpuCache, dimkey_idx1, lo_off, ht1, dim_len1, min_key1, num_tile_items);

  } else if (dimkey_idx1 == NULL && dim_off1 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off1 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset1, selection_flags, num_tile_items);
  }

  __syncthreads();

  if (dimkey_idx2 != NULL && dim_off2 == NULL) {

    if (lo_off == NULL) {
      int dimkey_seg2 = dimkey_idx2[segment_index];
      int* ptr = gpuCache + dimkey_seg2 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, dim_offset2, selection_flags, gpuCache, dimkey_idx2, lo_off, ht2, dim_len2, min_key2, num_tile_items);

  } else if (dimkey_idx2 == NULL && dim_off2 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off2 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset2, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (dimkey_idx3 != NULL && dim_off3 == NULL) {

    if (lo_off == NULL) {
      int dimkey_seg3 = dimkey_idx3[segment_index];
      int* ptr = gpuCache + dimkey_seg3 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, dim_offset3, selection_flags, gpuCache, dimkey_idx3, lo_off, ht3, dim_len3, min_key3, num_tile_items);

  } else if (dimkey_idx3 == NULL && dim_off3 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off3 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, dim_offset3, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (dimkey_idx4 != NULL && dim_off4 == NULL) {

    if (lo_off == NULL) {
      int dimkey_seg4 = dimkey_idx4[segment_index];
      int* ptr = gpuCache + dimkey_seg4 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    #pragma unroll
    for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
    {
      if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
        if (lo_off != NULL) {
          int dimkey_seg4 = dimkey_idx4[items_lo[ITEM] / SEGMENT_SIZE];
          items[ITEM] = gpuCache[dimkey_seg4 * SEGMENT_SIZE + (items_lo[ITEM] % SEGMENT_SIZE)];
        }

        // Out-of-bounds items are selection_flags
        int hash = HASH(items[ITEM], dim_len4, min_key4); //19920101
        if (selection_flags[ITEM]) {
          int slot = ht4[(hash << 1) + 1];
          if (slot != 0) {
            t_count++;
            dim_offset4[ITEM] = slot - 1;
          } else {
            selection_flags[ITEM] = 0;
          }
        }
      }
    }
  } else if (dimkey_idx4 == NULL && dim_off4 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off4 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    #pragma unroll
    for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
    {
      if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
        if (selection_flags[ITEM]) {
          t_count++;
          dim_offset4[ITEM] = items[ITEM];
        }
      }
    }
  } else {
    #pragma unroll
    for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
    {
      if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) { //?
        if (selection_flags[ITEM]) {
          t_count++;
        }
      }
    }
  }

  //Barrier
  __syncthreads();

  // TODO: need to check logic for offset
  BlockScanInt(temp_storage.scan).ExclusiveSum(t_count, c_t_count); //doing a prefix sum of all the previous threads in the block and store it to c_t_count
  if(threadIdx.x == blockDim.x - 1) { //if the last thread in the block, add the prefix sum of all the prev threads + sum of my threads to global variable total
      block_off = atomicAdd(total, t_count+c_t_count); //the previous value of total is gonna be assigned to block_off
  } //block_off does not need to be global (it's just need to be shared), because it will get the previous value from total which is global

  __syncthreads();

  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM) {
    if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
      if(selection_flags[ITEM]) {
        int offset = block_off + c_t_count++;
        out_lo_off[offset] = start_offset + tile_idx * tile_size + threadIdx.x * ITEMS_PER_THREAD + ITEM;
        if (out_dim_off1 != NULL) out_dim_off1[offset] = dim_offset1[ITEM];
        if (out_dim_off2 != NULL) out_dim_off2[offset] = dim_offset2[ITEM];
        if (out_dim_off3 != NULL) out_dim_off3[offset] = dim_offset3[ITEM];
        if (out_dim_off4 != NULL) out_dim_off4[offset] = dim_offset4[ITEM];
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__global__ void probe_group_by_GPU(int* dim_key1, int* dim_key2, int* dim_key3, int* dim_key4, int* aggr, 
  int fact_len, int* ht1, int dim_len1, int* ht2, int dim_len2, int* ht3, int dim_len3, int* ht4, int dim_len4, int* res,
  int min_val1, int unique_val1, int min_val2, int unique_val2, int min_val3, int unique_val3, int min_val4, int unique_val4,
  int total_val, int min_key1, int min_key2, int min_key3, int min_key4) {

  // Specialize BlockLoad for a 1D block of 128 threads owning 4 integer items each
  typedef cub::BlockLoad<int, BLOCK_THREADS, ITEMS_PER_THREAD, BLOCK_LOAD_TRANSPOSE> BlockLoadInt;
  
  int tile_size = BLOCK_THREADS * ITEMS_PER_THREAD;
  int tile_idx = blockIdx.x;    // Current tile index
  int tile_offset = tile_idx * tile_size;

  // Allocate shared memory for BlockLoad
  __shared__ union TempStorage
  {
    typename BlockLoadInt::TempStorage load_items;
  } temp_storage;

  // Load a segment of consecutive items that are blocked across threads
  int items[ITEMS_PER_THREAD];
  int selection_flags[ITEMS_PER_THREAD];
  int groupval1[ITEMS_PER_THREAD];
  int groupval2[ITEMS_PER_THREAD];
  int groupval3[ITEMS_PER_THREAD];
  int groupval4[ITEMS_PER_THREAD];
  int aggrval[ITEMS_PER_THREAD];

  int num_tiles = (fact_len + tile_size - 1) / tile_size;
  int num_tile_items = tile_size;
  bool is_last_tile = false;
  if (tile_idx == num_tiles - 1) {
    num_tile_items = fact_len - tile_offset;
    is_last_tile = true;
  }

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    selection_flags[ITEM] = 1;
  }
  __syncthreads();

  if (dim_key1 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key1 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval1, selection_flags, ht1, dim_len1, min_key1, num_tile_items);

  } else {

    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval1, selection_flags, num_tile_items);
  }

  __syncthreads();

  if (dim_key2 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key2 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval2, selection_flags, ht2, dim_len2, min_key2, num_tile_items);

  } else {
    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval2, selection_flags, num_tile_items);
  }

  __syncthreads();

  if (dim_key3 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key3 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval3, selection_flags, ht3, dim_len3, min_key3, num_tile_items);

  } else {
    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval3, selection_flags, num_tile_items);
  }

  __syncthreads();

  if (dim_key4 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_key4 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPU<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval4, selection_flags, ht4, dim_len4, min_key4, num_tile_items);

  } else {
    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval4, selection_flags, num_tile_items);
  }

  __syncthreads();

  BlockLoadInt(temp_storage.load_items).Load(aggr + tile_offset, aggrval, num_tile_items);

  // Barrier for smem reuse
  __syncthreads();

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM) {
    if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
      if (selection_flags[ITEM]) {
        int hash = ((groupval1[ITEM] - min_val1) * unique_val1 + (groupval2[ITEM] - min_val2) * unique_val2 +  (groupval3[ITEM] - min_val3) * unique_val3 + (groupval4[ITEM] - min_val4) * unique_val4) % total_val; //!
        res[hash * 6] = groupval1[ITEM];
        res[hash * 6 + 1] = groupval2[ITEM];
        res[hash * 6 + 2] = groupval3[ITEM];
        res[hash * 6 + 3] = groupval4[ITEM];
        atomicAdd(reinterpret_cast<unsigned long long*>(&res[hash * 6 + 4]), (long long)(aggrval[ITEM]));
      }
    }
  }
}

template<int BLOCK_THREADS, int ITEMS_PER_THREAD>
__global__ void probe_group_by_GPU2(int* lo_off, int* dim_off1, int* dim_off2, int* dim_off3, int* dim_off4,
  int* gpuCache, int* dimkey_idx1, int* dimkey_idx2, int* dimkey_idx3, int* dimkey_idx4, int* aggr_idx,
  int fact_len, int* ht1, int dim_len1, int* ht2, int dim_len2, int* ht3, int dim_len3, int* ht4, int dim_len4, int* res,
  int min_val1,int unique_val1, int min_val2, int unique_val2, int min_val3, int unique_val3, int min_val4, int unique_val4,
  int total_val, int min_key1, int min_key2, int min_key3, int min_key4, int start_offset, int* segment_group) {

  //assume start_offset always in the beginning of a segment (ga mungkin start di tengah2 segment)
  //assume tile_size is a factor of SEGMENT_SIZE (SEGMENT SIZE kelipatan tile_size)

  // Specialize BlockLoad for a 1D block of 128 threads owning 4 integer items each
  typedef cub::BlockLoad<int, BLOCK_THREADS, ITEMS_PER_THREAD, BLOCK_LOAD_TRANSPOSE> BlockLoadInt;
  
  int tile_size = BLOCK_THREADS * ITEMS_PER_THREAD;
  int tile_idx = blockIdx.x;    // Current tile index
  int tile_offset = tile_idx * tile_size;

  int tiles_per_segment = SEGMENT_SIZE/tile_size;
  int segment_index;
  if (segment_group == NULL)
    segment_index = ( start_offset + tile_offset ) / SEGMENT_SIZE;
  else {
    int idx = tile_offset / SEGMENT_SIZE;
    segment_index = segment_group[idx];
  }
  int segment_tile_offset = (tile_idx % tiles_per_segment) * tile_size; //tile offset inside a segment

  // Allocate shared memory for BlockLoad
  __shared__ union TempStorage
  {
    typename BlockLoadInt::TempStorage load_items;
  } temp_storage;

  // Load a segment of consecutive items that are blocked across threads
  int items[ITEMS_PER_THREAD];
  int items_lo[ITEMS_PER_THREAD];
  int selection_flags[ITEMS_PER_THREAD];
  int groupval1[ITEMS_PER_THREAD];
  int groupval2[ITEMS_PER_THREAD];
  int groupval3[ITEMS_PER_THREAD];
  int groupval4[ITEMS_PER_THREAD];
  int aggrval[ITEMS_PER_THREAD];

  int num_tiles = (fact_len + tile_size - 1) / tile_size;
  int num_tile_items = tile_size;
  bool is_last_tile = false;
  if (tile_idx == num_tiles - 1) {
    num_tile_items = fact_len - tile_offset;
    is_last_tile = true;
  }

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM)
  {
    selection_flags[ITEM] = 1;
  }

  __syncthreads();

  if (lo_off != NULL) BlockLoadInt(temp_storage.load_items).Load(lo_off + tile_offset, items_lo, num_tile_items);

  // Barrier for smem reuse
  __syncthreads();

  if (dim_off1 == NULL && dimkey_idx1 != NULL) { //normal operation, here dimkey_idx will be lo_partkey, lo_suppkey, etc (the join key column)

    // Barrier for smem reuse
    __syncthreads();

    if (lo_off == NULL) {
      int dimkey_seg1 = dimkey_idx1[segment_index];
      int* ptr = gpuCache + dimkey_seg1 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGroupByGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, groupval1, selection_flags, gpuCache, dimkey_idx1, lo_off, ht1, dim_len1, min_key1, num_tile_items);

  } else if (dim_off1 != NULL && dimkey_idx1 != NULL) { //we take the result from prev join in dim_off but we will also take the groupby column, here dimkey_idx will be the groupby column (d_year, p_brand1, etc.)
    BlockLoadInt(temp_storage.load_items).Load(dim_off1 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPUHelper2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval1, selection_flags, gpuCache, dimkey_idx1, num_tile_items);

  } else { //we take the result from prev join and will not take group by column (just act as filter)

    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval1, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (dim_off2 == NULL && dimkey_idx2 != NULL) {

    if (lo_off == NULL) {
      int dimkey_seg2 = dimkey_idx2[segment_index];
      int* ptr = gpuCache + dimkey_seg2 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGroupByGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, groupval2, selection_flags, gpuCache, dimkey_idx2, lo_off, ht2, dim_len2, min_key2, num_tile_items);

  }  else if (dim_off2 != NULL && dimkey_idx2 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off2 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPUHelper2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval2, selection_flags, gpuCache, dimkey_idx2, num_tile_items);

  } else {

    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval2, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (dim_off3 == NULL && dimkey_idx3 != NULL) {

    if (lo_off == NULL) {
      int dimkey_seg3 = dimkey_idx3[segment_index];
      int* ptr = gpuCache + dimkey_seg3 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGroupByGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, groupval3, selection_flags, gpuCache, dimkey_idx3, lo_off, ht3, dim_len3, min_key3, num_tile_items);

  } else if (dim_off3 != NULL && dimkey_idx3 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off3 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPUHelper2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval3, selection_flags, gpuCache, dimkey_idx3, num_tile_items);

  } else {

    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval3, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (dim_off4 == NULL && dimkey_idx4 != NULL) {

    if (lo_off == NULL) {
      int dimkey_seg4 = dimkey_idx4[segment_index];
      int* ptr = gpuCache + dimkey_seg4 * SEGMENT_SIZE;
      BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, items, num_tile_items);
      // Barrier for smem reuse
      __syncthreads();
    }

    BlockProbeGroupByGPU2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, items_lo, groupval4, selection_flags, gpuCache, dimkey_idx4, lo_off, ht4, dim_len4, min_key4, num_tile_items);

  } else if (dim_off4 != NULL && dimkey_idx4 != NULL) {
    BlockLoadInt(temp_storage.load_items).Load(dim_off4 + tile_offset, items, num_tile_items);

    // Barrier for smem reuse
    __syncthreads();

    BlockProbeGroupByGPUHelper2<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval4, selection_flags, gpuCache, dimkey_idx4, num_tile_items);

  } else {

    BlockProbeGroupByGPUHelper<BLOCK_THREADS, ITEMS_PER_THREAD>(threadIdx.x, items, groupval4, selection_flags, num_tile_items);

  }

  __syncthreads();

  if (lo_off == NULL) {
    int aggr_seg = aggr_idx[segment_index];
    int* ptr = gpuCache + aggr_seg * SEGMENT_SIZE;
    BlockLoadInt(temp_storage.load_items).Load(ptr + segment_tile_offset, aggrval, num_tile_items);
    // Barrier for smem reuse
    __syncthreads();
  }

  #pragma unroll
  for (int ITEM = 0; ITEM < ITEMS_PER_THREAD; ++ITEM) {
    if (!is_last_tile || (int(threadIdx.x * ITEMS_PER_THREAD) + ITEM < num_tile_items)) {
      if (lo_off != NULL) {
        int aggr_seg = aggr_idx[items_lo[ITEM] / SEGMENT_SIZE];
        aggrval[ITEM] = gpuCache[aggr_seg * SEGMENT_SIZE + (items_lo[ITEM] % SEGMENT_SIZE)];
      }

      if (selection_flags[ITEM]) {
        //printf("aggrval = %d\n", aggrval[ITEM]);
        int hash = ((groupval1[ITEM] - min_val1) * unique_val1 + (groupval2[ITEM] - min_val2) * unique_val2 +  (groupval3[ITEM] - min_val3) * unique_val3 + (groupval4[ITEM] - min_val4) * unique_val4) % total_val; //!
        res[hash * 6] = groupval1[ITEM];
        res[hash * 6 + 1] = groupval2[ITEM];
        res[hash * 6 + 2] = groupval3[ITEM];
        res[hash * 6 + 3] = groupval4[ITEM];
        atomicAdd(reinterpret_cast<unsigned long long*>(&res[hash * 6 + 4]), (long long)(aggrval[ITEM]));
      }
    }
  }
}

__global__
void build_GPU(int* dim_key, int* dim_val, int num_tuples, int *hash_table, int num_slots, int val_min, int segment_number, int isoffset) {
  int offset = blockIdx.x * blockDim.x + threadIdx.x;
  if (offset < num_tuples) {
    int key = dim_key[offset];
    int value;
    if (isoffset == 1) value = segment_number * SEGMENT_SIZE + offset + 1;
    else if (isoffset == 0) value = dim_val[offset];
    else value = 0;
    int hash = HASH(key, num_slots, val_min);
    atomicCAS(&hash_table[hash << 1], 0, key);
    //printf("%d\n", hash_table[hash << 1]);
    hash_table[(hash << 1) + 1] = value;
  }
}

__global__
void build_filter_GPU(int* filter_col, int compare, int* dim_key, int* dim_val, int num_tuples, int *hash_table, int num_slots, int val_min, int segment_number, int isoffset) {
  int offset = blockIdx.x * blockDim.x + threadIdx.x;
  if (offset < num_tuples) {
    if (filter_col[offset] == compare) {
      int key = dim_key[offset];
      int value;
      if (isoffset == 1) value = segment_number * SEGMENT_SIZE + offset + 1;
      else if (isoffset == 0) value = dim_val[offset];
      else value = 0;
      int hash = HASH(key, num_slots, val_min);
      atomicCAS(&hash_table[hash << 1], 0, key);
      hash_table[(hash << 1) + 1] = value;
    }
  }
}

__global__
void runAggregationQ2GPU(int* gpuCache, int* lo_idx, int* p_idx, int* d_idx, int* lo_off, int* part_off, int* date_off, int num_tuples, int* res, int num_slots) {
  int offset = blockIdx.x * blockDim.x + threadIdx.x;

  if (offset < num_tuples) {
    int revenue_idx = lo_off[offset];
    int brand_idx = part_off[offset];
    int year_idx = date_off[offset];

    int revenue_seg = lo_idx[revenue_idx / SEGMENT_SIZE];
    int brand_seg = p_idx[brand_idx / SEGMENT_SIZE];
    int year_seg = d_idx[year_idx / SEGMENT_SIZE];

    int revenue = gpuCache[revenue_seg * SEGMENT_SIZE + (revenue_idx % SEGMENT_SIZE)];
    int brand = gpuCache[brand_seg * SEGMENT_SIZE + (brand_idx % SEGMENT_SIZE)];
    int year = gpuCache[year_seg * SEGMENT_SIZE + (year_idx % SEGMENT_SIZE)];

    int hash = (brand * 7 + (year - 1992)) % num_slots;

    res[hash * 6] = 0;
    res[hash * 6 + 1] = brand;
    res[hash * 6 + 2] = year;
    atomicAdd(reinterpret_cast<unsigned long long*>(&res[hash * 6 + 4]), (long long)(revenue));

  }
}