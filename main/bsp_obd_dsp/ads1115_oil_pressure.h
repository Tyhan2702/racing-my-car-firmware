#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the oil pressure sampling task (ADS1115 I2C ADC, V1 Waveshare board only;
// vehicles with an OBD oil-pressure DID read it over OBD instead of this ADC).
void oil_pressure_start(void);

#ifdef __cplusplus
}
#endif
