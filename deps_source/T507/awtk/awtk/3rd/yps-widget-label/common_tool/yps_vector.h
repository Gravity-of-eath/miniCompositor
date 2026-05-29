/**
 * @file yps_vector.h
 * @author wangdongpo (dongpo.wang@carbit.com.cn)
 * @brief 这个文件是一个容器的实现，可以存放任意类型的数据，类似于C++的vector容器
 * @version 0.1
 * @date 2023-04-11
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef __YPS_VECTOR_H__
#define __YPS_VECTOR_H__

/**
 * @brief 容器结构体
 * 
 */
typedef struct yps_vector_t{
  void* elms;
  int size;
  int capacity;
  int elem_size;
} yps_vector;

/**
 * @brief 创建一个vector容器
 * 
 * @param elem_size 元素大小
 * @return yps_vector*  返回容器指针
 */
yps_vector* yps_vector_create(int elem_size);

/**
 * @brief 销毁容器
 * 
 * @param vec 容器指针
 */
void yps_vector_destroy(yps_vector* vec);

/**
 * @brief 向容器中添加元素
 * 
 * @param vec 容器指针
 * @param elem 元素指针
 */
void yps_vector_push_back(yps_vector* vec, void* elem);

/**
 * @brief 获取容器中元素个数
 * 
 * @param vec 容器指针
 * @return int 元素个数
 */
int yps_vector_size(yps_vector* vec);

/**
 * @brief 获取容器中元素
 * 
 * @param vec 容器指针
 * @param index 元素索引
 * @return void* 元素指针
 */
void* yps_vector_at(yps_vector* vec, int index);

/**
 * @brief 清空容器
 * 
 * @param vec 容器指针
 */
void yps_vector_clear(yps_vector* vec);

/**
 * @brief 删除容器中元素
 * 
 * @param vec 容器指针
 * @param index 元素索引
 */
void yps_vector_erase(yps_vector* vec, int index);

/**
 * @brief 删除容器中元素
 * 
 * @param vec 容器指针
 * @param index 元素索引
 * @param count 删除个数
 */
void yps_vector_erase_range(yps_vector* vec, int index, int count);

/**
 * @brief 容器是否为空
 * 
 * @param vec 容器指针
 * @return int 1为空，0不为空
 */
int yps_vector_empty(yps_vector* vec);

/**
 * @brief 容器中是否包含元素
 * 
 * @param vec 容器指针
 * @param elem 元素指针
 * @return int 1包含，0不包含
 */
int yps_vector_contains(yps_vector* vec, void* elem);

/**
 * @brief 容器中是否包含元素
 * 
 * @param vec 容器指针
 * @param elem 元素指针
 * @param compare_func 比较函数
 * @return int 1包含，0不包含
 */
int yps_vector_contains_ex(yps_vector* vec, void* elem, int (*compare_func)(void*, void*));

#endif