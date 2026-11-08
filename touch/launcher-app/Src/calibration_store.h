#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H
#include <stdbool.h>
bool calibration_load(float *coefficients);
bool calibration_save(const float *coefficients);
#endif
