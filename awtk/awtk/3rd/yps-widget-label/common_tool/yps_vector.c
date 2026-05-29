#include "yps_vector.h"
#include <stdlib.h>
#include <memory.h>
#ifndef NULL
#define NULL (void*)0
#endif

yps_vector* yps_vector_create(int elem_size) {
  yps_vector* v = (yps_vector*)malloc(sizeof(yps_vector));
  if(v == NULL){
      return NULL;
  }
  v->elem_size = elem_size;
  v->size = 0;
  v->capacity = 32;
  v->elms = malloc(elem_size);

  return v;
}

void yps_vector_destroy(yps_vector* v){
    if(v == NULL){
        return;
    }
    
    if(v->elms != NULL){
        free(v->elms);
    }
    
    free(v);
}

void yps_vector_clear(yps_vector* v){
    if (v == NULL) {
        return;
    }

    if(v->elms != NULL){
        free(v->elms);
        v->elms = NULL;
    }
    
    v->size = 0;
    v->capacity = 32;
}

void yps_vector_push_back(yps_vector* vec, void* elem){
    if (vec == NULL || elem == NULL) {
        return;
    }

    if(vec->size >= vec->capacity){
        vec->capacity *= 2;
        vec->elms = (void*)realloc(vec->elms, vec->capacity * vec->elem_size);
    }

    memcpy((char*)vec->elms + vec->size * vec->elem_size, (char*)elem, vec->elem_size);
    vec->size++;
}

int yps_vector_size(yps_vector* vec){
    if(vec == NULL){
        return 0;
    }
    
    return vec->size;
}

void* yps_vector_at(yps_vector* vec, int index){
    if(vec == NULL || index < 0 || index >= vec->size){
        return NULL;
    }
    return (char*)vec->elms + index * vec->elem_size;
}


void yps_vector_erase(yps_vector* vec, int index){
    if(vec == NULL || index < 0 || index >= vec->size){
        return;
    }
    if(index == vec->size - 1){
        vec->size--;
    }else{
        memmove((char*)vec->elms + index * vec->elem_size,
                (char*)vec->elms + (index + 1) * vec->elem_size,
                (vec->size - index - 1) * vec->elem_size);
        vec->size--;
    }
}

void yps_vector_erase_range(yps_vector* vec, int index, int count){
    if(vec == NULL || index < 0 || index >= vec->size || count <= 0){
        return;
    }
    
    if(index + count >= vec->size){
        vec->size = index;
    }else{
        memmove((char*)vec->elms + index * vec->elem_size,
                (char*)vec->elms + (index + count) * vec->elem_size,
                (vec->size - index - count) * vec->elem_size);
        vec->size -= count;
    }
}

int yps_vector_empty(yps_vector* vec){
    if(vec == NULL){
        return 1;
    }
    
    return vec->size == 0;
}

int yps_vector_contains(yps_vector* vec, void* elem){
    if(vec == NULL || elem == NULL){
        return 0;
    }
    
    int i = 0;
    for(i = 0; i < vec->size; i++){
        if (memcmp((char*)vec->elms + i * vec->elem_size, (char*)elem, vec->elem_size) == 0) {
          return 1;
        }
    }
    
    return 0;
}

int yps_vector_contains_ex(yps_vector* vec, void* elem, int (*compare_func)(void*, void*)) {
    if(vec == NULL || elem == NULL || compare_func == NULL){
        return 0;
    }
    
    int i = 0;
    for(i = 0; i < vec->size; i++){
        if (compare_func((char*)vec->elms + i * vec->elem_size, elem) == 0) {
          return 1;
        }
    }
    
    return 0;
}
