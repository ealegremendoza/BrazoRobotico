#ifndef SERVO_UNITS_H
#define SERVO_UNITS_H

#include <stdint.h>

int32_t mrad_to_counts(int32_t mrad);
int32_t counts_to_mrad(int32_t counts);

#endif // SERVO_UNITS_H
