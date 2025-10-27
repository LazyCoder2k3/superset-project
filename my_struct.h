#ifndef MY_STRUCT_H
#define MY_STRUCT_H

#define EMBEDDING_DIM 128

typedef struct {
    float value;
} Val;

typedef struct {
    Val vals[EMBEDDING_DIM];
    int feature_num;
} Voiceprint_feature;


#endif
