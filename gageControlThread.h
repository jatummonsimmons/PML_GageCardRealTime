#ifndef GAGETHREAD_H
#define GAGETHREAD_H

#include <QMutex>
#include <QSize>
#include <QThread>
#include <QWaitCondition>
#include <QTimer>
#include <QDebug>
#include <windows.h>

#include <iostream>
#include <stdio.h>
#include <cstdint>
#include <stdlib.h>

#include "CsPrototypes.h"
#include "CsExpert.h"
#include "CsTchar.h"
#include "CsAppSupport.h"

#include <acquisitionConfig.h>

class gageControlThread : public QThread
{
    Q_OBJECT

public:
    gageControlThread(QObject *parent = nullptr);
    ~gageControlThread();
    void readLatestData(QVector< QVector<double> > *ch1,
                        QVector< QVector<double> > *ch2,
                        QVector< QVector<double> > *ch3,
                        QVector< QVector<double> > *ch4);
    void stopRunning();
    int saveDataBuffer();
    void startContinuousSave(int numBuffer);
    void stopContinuousSave();

signals:
    void dataReady(gageControlThread * dataThread);
    void continuousSaveComplete();

protected:
    void run() override;

private:

    typedef struct
    {
        uInt32		u32TransferTimeout;
        TCHAR		strResultFile[MAX_PATH];
        BOOL		bDoAnalysis;			/* Turn on or off data analysis */
    }CSSTMCONFIG, *PCSSTMCONFIG;

    int32 InitializeStream(CSHANDLE hSystem);
    int64 SumBufferData(void* pBuffer, uInt32 u32Size, uInt32 u32SampleBits);
    void UpdateProgress(DWORD dwTickStart, uInt32 u32UpdateInterval, unsigned long long llSamples);
    void SaveResults(int16 * saveData);
    int32 LoadStmConfiguration(LPCTSTR szIniFile, PCSSTMCONFIG pConfig);

    // constants for processing
    uint32_t preTriggerSamples;
    uint32_t postTriggerSamples;
    uint32_t recordsPerBuffer;
    uint32_t buffersPerAcquisition;
    uint32_t bytesPerBuffer;

    // atomics for handling thread communication
    std::atomic<uint32_t> numSaveBufferAtom;
    std::atomic<bool> flagAtom;

    // Setup for variables for buffer
    int16 * saveBuffer;
    int totalBufferCount;

    // management of threads
    QMutex mutex;
    bool flag;
    bool running;
    bool pauseSaveBuffer;

    // Declarations for continuous save operations including startContinuousSave() and stopContinuousSave()
    bool saveData;
    uint32_t currentSaveCount;
    uint32_t totalSaveCount;
    FILE *continuousSaveFile;

};

#endif // GAGETHREAD_H
