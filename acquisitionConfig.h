#ifndef ACQUISITIONCONFIG_H
#define ACQUISITIONCONFIG_H

// Acquisition parameters
#define PRE_TRIGGER_SAMPLES (128)
#define POST_TRIGGER_SAMPLES (640)

// Other parameters for acquisition
#define RECORDS_PER_BUFFER (512)
#define BUFFERS_PER_ACQUISITION (1000)
#define BUFFERS_PER_SAVEBUFFER (500)

// Processing parameters
#define NUM_AVERAGE_SIGNALS (20)
#define NUM_PIXELS (200)
#define MIRROR_VOLTAGE_RANGE_PM_V (6000)

// Save parameters
#define SAVE_PATH ("C:\\Users\\labadmin\\Documents\\RealTime_DataSaveDirectory\\")
#endif
