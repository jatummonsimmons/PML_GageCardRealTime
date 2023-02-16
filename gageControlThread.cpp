#include "gageControlThread.h"

#define TRANSFER_TIMEOUT	10000
#define STM_SECTION _T("StmConfig")				/* section name in ini file */
#define MAX_SEGMENTS_COUNT	100000000			// Max number of segments in a single file

gageControlThread :: gageControlThread(QObject *parent):
    QThread(parent),
    flag(false),
    running(true),
    pauseSaveBuffer(false)
{
    numSaveBufferAtom = 0;
    flagAtom = false;
}

gageControlThread :: ~gageControlThread()
{
    wait();
    delete[] saveBuffer;
}

void gageControlThread::run()
{
    int32						i32Status = CS_SUCCESS;
    void*						pBuffer1 = NULL;
    void*						pBuffer2 = NULL;
    void*						pCurrentBuffer = NULL;
    void*						pWorkBuffer = NULL;
    uInt32						u32Mode;
    CSHANDLE					hSystem = 0;
    CSSYSTEMINFO				CsSysInfo = {0};
    CSACQUISITIONCONFIG			CsAcqCfg = {0};
    CSSTMCONFIG					StmConfig;
    LPCTSTR						szIniFile = _T("C:\\Users\\labadmin\\Documents\\GitHub\\PML_GageCardRealTime\\Stream2Analysis.ini");
    BOOL						bDone = FALSE;
    BOOL						bErrorData = FALSE;
    uInt32						u32LoopCount = 0;
    uInt32						u32SaveCount = 0;
    uInt32						u32TickStart = 0;
    long long					llTotalSamples = 0;
    uInt32						u32TransferSize = 0;
    uInt32						u32DataSegmentInBytes = 0;
    uInt32						u32DataSegmentWithTailInBytes = 0;
    uInt32						u32ErrorFlag = 0;
    //int64*						pi64Sums;
    uInt32						u32ActualLength = 0;
    uInt8						u8EndOfData = 0;
    BOOL						bStreamCompletedSuccess = FALSE;
    uInt32						u32SegmentTail_Bytes = 0;

    //JATS CHANGE: Added variables
    uInt32                      u32BufferSizeWithTailInBytes = 0;

    ////////////////////////////////////////////////////////////////////////////////////
    /// Initialize Gage Card
    ////////////////////////////////////////////////////////////////////////////////////
    // Initializes the CompuScope boards found in the system. If the
    // system is not found a message with the error code will appear.
    // Otherwise i32Status will contain the number of systems found.
    i32Status = CsInitialize();

    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        return;
    }

    // Get System. This sample program only supports one system. If
    // 2 systems or more are found, the first system that is found
    // will be the system that will be used. hSystem will hold a unique
    // system identifier that is used when referencing the system.
    i32Status = CsGetSystem(&hSystem, 0, 0, 0, 0);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        return;
    }

    // Get System information. The u32Size field must be filled in
    // prior to calling CsGetSystemInfo
    CsSysInfo.u32Size = sizeof(CSSYSTEMINFO);
    i32Status = CsGetSystemInfo(hSystem, &CsSysInfo);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        CsFreeSystem(hSystem);
        return;
    }

    if ( CsSysInfo.u32BoardCount > 1 )
    {
        _ftprintf(stdout, _T("This sample program does not support CompuScope M/S system."));
        CsFreeSystem(hSystem);
        return;
    }

    // Display the system name from the driver
    //_ftprintf(stdout, _T("\nBoard Name: %s"), CsSysInfo.strBoardName);

    // In this example we're only using 1 trigger source
    i32Status = CsAs_ConfigureSystem(hSystem, (int)CsSysInfo.u32ChannelCount, 1, (LPCTSTR)szIniFile, &u32Mode);

    if (CS_FAILED(i32Status))
    {
        if (CS_INVALID_FILENAME == i32Status)
        {
            // Display message but continue on using defaults.
            _ftprintf(stdout, _T("\nCannot find %s - using default parameters."), szIniFile);
        }
        else
        {
            // Otherwise the call failed.  If the call did fail we should free the CompuScope
            // system so it's available for another application
            DisplayErrorString(i32Status);
            CsFreeSystem(hSystem);
            return;
        }
    }

    // If the return value is greater than  1, then either the application,
    // acquisition, some of the Channel and / or some of the Trigger sections
    // were missing from the ini file and the default parameters were used.
    if (CS_USING_DEFAULT_ACQ_DATA & i32Status)
        _ftprintf(stdout, _T("\nNo ini entry for acquisition. Using defaults."));

    if (CS_USING_DEFAULT_CHANNEL_DATA & i32Status)
        _ftprintf(stdout, _T("\nNo ini entry for one or more Channels. Using defaults for missing items."));

    if (CS_USING_DEFAULT_TRIGGER_DATA & i32Status)
        _ftprintf(stdout, _T("\nNo ini entry for one or more Triggers. Using defaults for missing items."));


    // Load application specific information from the ini file
    i32Status = LoadStmConfiguration(szIniFile, &StmConfig);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        CsFreeSystem(hSystem);
        return;
    }
    if (CS_USING_DEFAULTS == i32Status)
    {
        _ftprintf(stdout, _T("\nNo ini entry for Stm configuration. Using defaults."));
    }

    // Streaming Configuration
    // Validate if the board supports hardware streaming  If  it is not supported,
    // we'll exit gracefully.
    i32Status = InitializeStream(hSystem);
    if (CS_FAILED(i32Status))
    {
        // Error string is displayed in InitializeStream
        CsFreeSystem(hSystem);
        return;
    }

    // Get user's acquisition data to use for various parameters for transfer
    CsAcqCfg.u32Size = sizeof(CSACQUISITIONCONFIG);
    i32Status = CsGet(hSystem, CS_ACQUISITION, CS_CURRENT_CONFIGURATION, &CsAcqCfg);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        CsFreeSystem(hSystem);
        return;
    }

    // For this sample program only, check for a limited segment size
    if ( CsAcqCfg.i64SegmentSize < 0 || CsAcqCfg.i64Depth < 0 )
    {
        _ftprintf (stderr, _T("\n\nThis sample program does not support acquisitions with infinite segment size.\n"));
        CsFreeSystem(hSystem);
        return;
    }

//    if ( CsAcqCfg.u32SegmentCount > MAX_SEGMENTS_COUNT )
//    {
//        _ftprintf (stderr, _T("\nSegment count too big  %d. Max segment count support = %d \n"),
//                    CsAcqCfg.u32SegmentCount, MAX_SEGMENTS_COUNT);
//        CsFreeSystem(hSystem);
//        return;
//    }

    // Allocate memory to keep the analysis results
//    pi64Sums = (int64 *)calloc(CsAcqCfg.u32SegmentCount, sizeof(int64));
//    if (NULL == pi64Sums)
//    {
//        _ftprintf (stderr, _T("\nUnable to allocate memory to hold analysis results.\n"));
//        CsFreeSystem(hSystem);
//        return;
//    }

    // In Streaming, some hardware related information are placed at the end of each segment. These samples contain also timestamp\
    // information for the segemnt. The number of extra bytes may be different depending on CompuScope card model.
    i32Status = CsGet( hSystem, 0, CS_SEGMENTTAIL_SIZE_BYTES, &u32SegmentTail_Bytes );
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        ExitThread(1);
    }


    // We need to allocate a buffer for transferring the data. Buffer is allocated as void with
    // a size of length * number of channels * sample size. All channels in the mode are transferred
    // within the same buffer, so the mode tells us the number of channels.
    // The buffer must be allocated by a call to CsStmAllocateBuffer.  This routine will
    // allocate a buffer suitable for streaming.  In this program we're allocating 2 streaming buffers
    // so we can transfer to one while doing analysis on the other.
    u32DataSegmentInBytes = (uInt32)(CsAcqCfg.i64SegmentSize * CsAcqCfg.u32SampleSize *(CsAcqCfg.u32Mode & CS_MASKED_MODE));

    u32DataSegmentWithTailInBytes = u32DataSegmentInBytes + u32SegmentTail_Bytes;

    // Round the buffersize to a 16 byte boundary. This is necessary for streaming.
    u32DataSegmentWithTailInBytes = u32DataSegmentWithTailInBytes/16*16 ;

    // JATS CHANGE: Adding a u32 variable that is actual buffer size (choosing 128 as it is divisible by 16 and is big enough to avoid the card internally buffering)
    int numberOfSegmentsInBuffer = RECORDS_PER_BUFFER;
    // JATS CHANGE: Rounding this segments in buffer number to 16. re: comment above about necessity for streaming.
    numberOfSegmentsInBuffer = numberOfSegmentsInBuffer/16*16;
    u32BufferSizeWithTailInBytes = u32DataSegmentWithTailInBytes*numberOfSegmentsInBuffer;


    // JATS CHANGE: This seems to represent the buffer size in units of sample (so number of int16s)
    u32TransferSize = u32BufferSizeWithTailInBytes/CsAcqCfg.u32SampleSize;

    //_ftprintf (stderr, _T("\n(Actual buffer size used for data streaming = %u Bytes)\n"), u32DataSegmentWithTailInBytes );
    qDebug() << "Actual buffer size used for data streaming = " << u32BufferSizeWithTailInBytes << "(Bytes)";

    i32Status = CsStmAllocateBuffer(hSystem, 0,  u32BufferSizeWithTailInBytes, &pBuffer1);
    if (CS_FAILED(i32Status))
    {
        _ftprintf (stderr, _T("\nUnable to allocate memory for stream buffer 1.\n"));
//        free(pi64Sums);
        CsFreeSystem(hSystem);
        return;
    }

    i32Status = CsStmAllocateBuffer(hSystem, 0,  u32BufferSizeWithTailInBytes, &pBuffer2);
    if (CS_FAILED(i32Status))
    {
        _ftprintf (stderr, _T("\nUnable to allocate memory for stream buffer 2.\n"));
        CsStmFreeBuffer(hSystem, 0, pBuffer1);
//        free(pi64Sums);
        CsFreeSystem(hSystem);
        return;
    }


    // Commit the values to the driver.  This is where the values get sent to the
    // hardware.  Any invalid parameters will be caught here and an error returned.
    i32Status = CsDo(hSystem, ACTION_COMMIT);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
//        free(pi64Sums);
        CsStmFreeBuffer(hSystem, 0, pBuffer1);
        CsStmFreeBuffer(hSystem, 0, pBuffer2);
        CsFreeSystem(hSystem);
        return;
    }

    ////////////////////////////////////////////////////////////////////////////////////
    /// Start Gage Card Streaming
    ////////////////////////////////////////////////////////////////////////////////////

    printf ("\nStart streaming. Press ESC to abort\n\n");

    // Start the data acquisition
    i32Status = CsDo(hSystem, ACTION_START);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
//        free(pi64Sums);
        CsStmFreeBuffer(hSystem, 0, pBuffer1);
        CsStmFreeBuffer(hSystem, 0, pBuffer2);
        CsFreeSystem(hSystem);
        return;
    }


    u32TickStart =  GetTickCount();

    // Loop until either we've done the number of segments we want, or running is turned false.
    // While we loop we transfer into pCurrentBuffer and do our analysis on pWorkBuffer

    // JATS CHANGE: changes to make it work with infinite buffers
//    dataBuffer = new int16[u32DataSegmentInBytes/2*CsAcqCfg.u32SegmentCount];
    UINT32 numberOfBuffersInSaveBuffer = BUFFERS_PER_SAVEBUFFER;
    UINT32 saveBufferSegmentCount = numberOfSegmentsInBuffer*numberOfBuffersInSaveBuffer;
    saveBuffer = new int16[u32DataSegmentInBytes/2*saveBufferSegmentCount];
    totalBufferCount = 0;
    bool sendEmit = false;
    flag = false;

    bytesPerBuffer = numberOfSegmentsInBuffer*u32DataSegmentInBytes;
    buffersPerAcquisition = numberOfBuffersInSaveBuffer;

    preTriggerSamples = PRE_TRIGGER_SAMPLES;
    postTriggerSamples = POST_TRIGGER_SAMPLES;

    std::atomic<bool> sendEmitAtom = false;


    while( ! ( bDone || bStreamCompletedSuccess) && running)
    {
        // Determine where new data transfer data will go. We alternate
        // between our 2 DMA buffers
        if (u32LoopCount & 1)
        {
            pCurrentBuffer = pBuffer2;
        }
        else
        {
            pCurrentBuffer = pBuffer1;
        }

        i32Status = CsStmTransferToBuffer(hSystem, 0, pCurrentBuffer, u32TransferSize);
        if (CS_FAILED(i32Status))
        {
            DisplayErrorString(i32Status);
            break;
        }

        if ( NULL != pWorkBuffer && (!pauseSaveBuffer))
        {
            // JATS CHANGE: saving buffer to larger data buffer which is the size of the acquisition
            // JATS CHANGE: also remove tail from each captured segment
            int16 * tempBuff = (int16*)pWorkBuffer;
            for( int i = 0; i < numberOfSegmentsInBuffer; i++){
                for(int j =0; j < u32DataSegmentInBytes/2; j++){
                    saveBuffer[j+i*u32DataSegmentInBytes/2+(numSaveBufferAtom%numberOfBuffersInSaveBuffer)*numberOfSegmentsInBuffer*u32DataSegmentInBytes/2] = tempBuff[j+i*u32DataSegmentWithTailInBytes/2];
                }
            }

            // Alternative method
            numSaveBufferAtom++;
            sendEmitAtom = !flagAtom;
            flagAtom = true;

            if (sendEmitAtom){
                //qDebug() << "Emit sent on buffer number" << numSaveBufferAtom;
                emit dataReady(this);
            }
            else {
                //qDebug() << "Emit not sent on buffer number:" << numSaveBufferAtom;
            }

            //qDebug()<< "Buffer number: " << totalBufferCount;

        }

        if (saveData) {
            if (currentSaveCount < totalSaveCount){
                // Write record to file
                currentSaveCount++;

                size_t bytesWritten;
                if (fwrite != NULL){
                    int16 * pBuffer = saveBuffer+((numSaveBufferAtom-1)%buffersPerAcquisition)*bytesPerBuffer/2;
                    bytesWritten = fwrite(pBuffer, sizeof(BYTE), bytesPerBuffer, continuousSaveFile);
                }
                else {
                    qDebug() << "File handle is null on trying to write before currentSaveCount < totalSaveCount";
                    stopContinuousSave();
                }

                if (bytesWritten != bytesPerBuffer)
                {
                    qDebug() << "Error: Write buffer" << numSaveBufferAtom << "failed --" << GetLastError();
                    stopContinuousSave();
                }
                else {
                    qDebug() << "Buffer number " << currentSaveCount << "saved successfully";
                }
            }
            else {
                stopContinuousSave();
            }
        }

        // Wait for the DMA transfer on the current buffer to complete so we can loop
        // back around to start a new one. Calling thread will sleep until
        // the transfer completes

        i32Status = CsStmGetTransferStatus( hSystem, 0, StmConfig.u32TransferTimeout, &u32ErrorFlag, &u32ActualLength, &u8EndOfData );
        if ( CS_SUCCEEDED(i32Status) )
        {
            llTotalSamples += u32TransferSize;
            bStreamCompletedSuccess = (0 != u8EndOfData);
        }
        else
        {
            bDone = TRUE;
            if ( CS_STM_TRANSFER_TIMEOUT == i32Status )
            {
                // Timeout on CsStmGetTransferStatus().
                // Data Stransfer has not yet completed. We can repeat calling CsStmGetTransferStatus()  until the function
                // returns the status CS_SUCCESS.
                // In this sample program, we just stop and return the error.
                DisplayErrorString(i32Status);
            }
            else /* some other error */
            {
                DisplayErrorString(i32Status);
            }
        }

        pWorkBuffer = pCurrentBuffer;

        u32LoopCount++;

    }

    // Abort the current acquisition
    CsDo(hSystem, ACTION_ABORT);


    // Because the loop count above tests at the start of the loop, we may break the loop
    // while there's still data in pWorkBuffer to analyze. That would be the last data transferred
    // into pBuffer2. So need to process
//    if ( StmConfig.bDoAnalysis )
//    {
//        //TODO: Need to double check that  this is a sensible operation in infinite acquisition...
//        if ( u32SaveCount < CsAcqCfg.u32SegmentCount )
//        {
//            int16 * tempBuff = (int16*)pWorkBuffer;
//            // JATS CHANGE: note the CsAcqCfg.u32SegmentCount%numberOfSegmentsInBuffer is to handle the last buffer where it will be partially filled
//            // JATS CHANGE: note the line with just numberOfSegmentsInBuffer assumes that we have infinite acqusition mode running
//            //for( int i = 0; i < CsAcqCfg.u32SegmentCount%numberOfSegmentsInBuffer; i++){
//            for( int i = 0; i < numberOfSegmentsInBuffer; i++){
//                for(int j =0; j < u32DataSegmentInBytes/2; j++){
//                    saveBuffer[j+i*u32DataSegmentInBytes/2+(totalBufferCount%numberOfBuffersInSaveBuffer)*numberOfSegmentsInBuffer*u32DataSegmentInBytes/2] = tempBuff[j+i*u32DataSegmentWithTailInBytes/2];
//                }
//                //qDebug() << "Segment number: "<< i;
//            }
//            qDebug() << "\n" << "Buffer number: " << totalBufferCount << "\n";
//        }

//        //JATS CHANGE: GUI interaction code
//        {
//            QMutexLocker locker(&mutex);
//            totalBufferCount++;
//            sendEmit = !flag;
//            flag = true;
//        }
//        if (sendEmit){
//            emit dataReady(this);
//        }
//    }


    //////////////////////////////////////////////////////////////
    // Free system buffers / compuscope systems
    //////////////////////////////////////////////////////////////

    // Free all buffers that have been allocated. Buffers allocated with
    // CsStmAllocateBuffer must be freed with CsStmFreeBuffer
    CsStmFreeBuffer(hSystem, 0, pBuffer1);
    CsStmFreeBuffer(hSystem, 0, pBuffer2);
    // Free the CompuScope system and any resources it's been using
    i32Status = CsFreeSystem(hSystem);

    if (bErrorData)
    {
        _ftprintf (stdout, _T("\nStream aborted on error.\n"));
    }
    else
    {
        if ( u32LoopCount < CsAcqCfg.u32SegmentCount )
        {
            _ftprintf (stdout, _T("\nStream aborted by user or error.\n"));
        }
        else
        {
            _ftprintf (stdout, _T("\nStream has finsihed %u loops.\n"), CsAcqCfg.u32SegmentCount);
        }
    }

    // JATS CHANGE: print out raw data list to binary file
    //FILE *myFile;
    //errno_t err = fopen_s(&myFile,"C:\\Users\\labadmin\\Documents\\GitHub\\Temp_DataOutput\\test.bin","wb");
//    fwrite(reinterpret_cast<char*>(dataBuffer),1,u32DataSegmentInBytes/2*CsAcqCfg.u32SegmentCount*2,myFile);
    //fwrite(reinterpret_cast<char*>(dataBuffer),1,u32DataSegmentInBytes/2*dataBufferSegmentCount*2,myFile);
    //err = fclose(myFile);
    //free(pi64Sums);

    return;
}

void gageControlThread::readLatestData(QVector< QVector <double> > *ch1,
                                       QVector< QVector <double> > *ch2,
                                       QVector< QVector <double> > *ch3,
                                       QVector< QVector <double> > *ch4)
{
    //U32 startTickCount = GetTickCount();
    uint64 totalSamplesPerChannelPerRecord = preTriggerSamples + postTriggerSamples;
    UINT32 tempBuffersCompleted = 0;
//    {
//        QMutexLocker locker(&mutex);
//        tempBuffersCompleted = numberOfBuffersAddedToSaveBuffer;
//        flag = false;
//    }
    tempBuffersCompleted = numSaveBufferAtom;
    flagAtom = false;

    int16 * windowBuffer = saveBuffer+((tempBuffersCompleted-1)%buffersPerAcquisition)*bytesPerBuffer/2;
    //int16 offset = 0x8000;
    int16 offset = 0;

    // *(1/4) represents the bit shift moving from int 16 to int 14
    // *(1/8192) respresents the scaling of this range from -8192 to 8192 to -1 to 1
    // *voltageRange represents the scaling of -1 to 1 to -Voltage max to +Voltage max
//    double ch1_scale = InputRangeIdToVolts(inputRange[0])*(1/8192.0)*(1/4.0);
//    double ch2_scale = InputRangeIdToVolts(inputRange[1])*(1/8192.0)*(1/4.0);
//    double ch3_scale = InputRangeIdToVolts(inputRange[2])*(1/8192.0)*(1/4.0);
//    double ch4_scale = InputRangeIdToVolts(inputRange[3])*(1/8192.0)*(1/4.0);

    double ch1_scale = 1.0;
    double ch2_scale = 1.0;
    double ch3_scale = 1.0;
    double ch4_scale = 1.0;

    uint64 index_offset;
    for (int i = 0; i < RECORDS_PER_BUFFER; i++){
        // could put a dereferencing section here for the first loop for two dimensional vectors to save some compute time
        index_offset = i*totalSamplesPerChannelPerRecord*4;
        for (int j = 0; j < totalSamplesPerChannelPerRecord; j++){
            (*ch1)[i][j] = ch1_scale*(windowBuffer[j*4+0+index_offset]-offset);
            (*ch2)[i][j] = ch2_scale*(windowBuffer[j*4+1+index_offset]-offset);
            (*ch3)[i][j] = ch3_scale*(windowBuffer[j*4+2+index_offset]-offset);
            (*ch4)[i][j] = ch4_scale*(windowBuffer[j*4+3+index_offset]-offset);
        }
    }
    //qDebug() << (*ch3)[0][0];
    //double transferTime_sec = (GetTickCount() - startTickCount) / 1000.;
    //qDebug() << transferTime_sec;
    return;
}

void gageControlThread::stopRunning()
{
    running = false;
    return;
}

void gageControlThread::startContinuousSave(int numBuffer)
{
    // initialize counters for saving
    totalSaveCount = numBuffer;
    currentSaveCount = 0;
    continuousSaveFile = NULL;

    // generate file name based on date and time
    time_t t = time(NULL);
    struct tm buff;
    localtime_s(&buff,&t);
    char dateTime[100];
    std::strftime(dateTime,sizeof(dateTime),"%Y-%m-%d-%H-%M-%S",&buff);
    std::string fileName = SAVE_PATH;
    fileName = fileName + dateTime + "-data.bin";

    // open file
    continuousSaveFile = fopen(fileName.c_str(), "wb");
    if (continuousSaveFile == NULL)
    {
        qDebug() << "Error: Unable to create data file --" << GetLastError();
        stopContinuousSave();
        return;
    }

    // enable writing to the file
    saveData = true;
    qDebug() << saveData;
    return;
}

void gageControlThread::stopContinuousSave()
{
    // check if the file was opened successfully and if so close it
    saveData = false;
    if (continuousSaveFile != NULL)
        fclose(continuousSaveFile);
    emit continuousSaveComplete();
    return;
}

int gageControlThread::saveDataBuffer()
{
    // Pause system saveBuffer write
    pauseSaveBuffer = true;
    qDebug() << "Starting file save...";
    FILE *fpData = NULL;
    size_t bytesWritten;

    //find date and time and concatenate
    time_t t = time(NULL);
    struct tm buff;
    localtime_s(&buff,&t);
    char dateTime[100];
    std::strftime(dateTime,sizeof(dateTime),"%Y-%m-%d-%H-%M-%S",&buff);
    std::string fileName = SAVE_PATH;
    fileName = fileName + dateTime + "-data.bin";

    // open file
    fpData = fopen(fileName.c_str(), "wb");
    if (fpData == NULL)
    {
        qDebug() << "Error: Unable to create data file --" << GetLastError();
        return 1;
    }
    qDebug() << "Successfully created data file";

    UINT32 tempBuffersCompleted = numSaveBufferAtom;
    if (tempBuffersCompleted < BUFFERS_PER_SAVEBUFFER){
        qDebug() << "Trying to save: " << tempBuffersCompleted*bytesPerBuffer;
        bytesWritten = fwrite(saveBuffer, sizeof(BYTE), tempBuffersCompleted*bytesPerBuffer, fpData);

        // check if errored
        if (bytesWritten != tempBuffersCompleted*bytesPerBuffer){
            qDebug() << "Error: Write buffer failed --";
            qDebug() << "Bytes written 1: " << bytesWritten << "does not equal" << tempBuffersCompleted*bytesPerBuffer;
            if (fpData != NULL)
                fclose(fpData);
            pauseSaveBuffer = false;
            return 1;
        }
    }
    else{
        // we have an array with [[5] [2] [3] [4]]
        // buffersCompleted is 5, so 5%4 is 1
        // so first fwrite from buffersCompleted to end of file
        bytesWritten = fwrite(&saveBuffer[(tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER)*(bytesPerBuffer/2)],
                              sizeof(BYTE),
                              (BUFFERS_PER_SAVEBUFFER-(tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER))*bytesPerBuffer,
                              fpData);
        if ((tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER) != 0){
            // if the buffer does not fit perfectly,
            // then fwrite from beginning of file to buffersCompleted
            bytesWritten += fwrite(saveBuffer,
                                   sizeof(BYTE),
                                   ((tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER))*bytesPerBuffer,
                                   fpData);
        }

        // check if errored
        if (bytesWritten != BUFFERS_PER_SAVEBUFFER*bytesPerBuffer){
            qDebug() << "Error: Write buffer failed";
            qDebug() << "Bytes written 2: " << bytesWritten << "does not equal" << BUFFERS_PER_SAVEBUFFER*bytesPerBuffer;
            if (fpData != NULL)
                fclose(fpData);
            pauseSaveBuffer = false;
            return 1;
        }
    }

    // Close file
    if (fpData != NULL)
        fclose(fpData);

    /* COMMENT IN FOR TIME STAMPS
    // Write the time stamp buffer
    fpData = fopen("timeStamp.bin", "wb");
    bytesWritten = 0;

    if (tempBuffersCompleted < BUFFERS_PER_SAVEBUFFER){
        bytesWritten = fwrite(waitTimeBuffer,sizeof(BYTE),tempBuffersCompleted*8*2,fpData);
        if (bytesWritten != tempBuffersCompleted*8*2){
            qDebug() << "Error: Write buffer failed --";
            if (fpData != NULL)
                fclose(fpData);
            return 1;
        }
    }
    else{
        bytesWritten = fwrite(&waitTimeBuffer[(tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER)*2],
                                sizeof(BYTE),
                                (BUFFERS_PER_SAVEBUFFER-(tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER))*8*2,
                                fpData);
        if ((tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER) != 0){
            // if the buffer does not fit perfectly,
            // then fwrite from beginning of file to buffersCompleted
            bytesWritten += fwrite(waitTimeBuffer,
                                   sizeof(BYTE),
                                   ((tempBuffersCompleted%BUFFERS_PER_SAVEBUFFER))*8*2,
                                   fpData);
        }

        if (bytesWritten != BUFFERS_PER_SAVEBUFFER*8*2){
            qDebug() << "Error: Write buffer failed";
            qDebug() << "Bytes written: " << bytesWritten << "does not equal" << BUFFERS_PER_SAVEBUFFER*bytesPerBuffer;
            if (fpData != NULL)
                fclose(fpData);
            return 1;
        }
    }

    // Close file
    if (fpData != NULL)
        fclose(fpData);
    */
    // Unpause system
    pauseSaveBuffer = false;
    qDebug() << "File save finished successfully";
    return 0;
}

// Functions inherited from Gage codebase
int32 gageControlThread::InitializeStream(CSHANDLE hSystem)
{
    int32	i32Status = CS_SUCCESS;
    int64	i64ExtendedOptions = 0;

    CSACQUISITIONCONFIG CsAcqCfg = {0};

    CsAcqCfg.u32Size = sizeof(CSACQUISITIONCONFIG);

    // Get user's acquisition Data
    i32Status = CsGet(hSystem, CS_ACQUISITION, CS_CURRENT_CONFIGURATION, &CsAcqCfg);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        return (i32Status);
    }

    // Check if selected system supports Expert Stream
    // And set the correct image to be used.
    CsGet(hSystem, CS_PARAMS, CS_EXTENDED_BOARD_OPTIONS, &i64ExtendedOptions);

    if (i64ExtendedOptions & CS_BBOPTIONS_STREAM)
    {
        _ftprintf(stdout, _T("\nSelecting Expert Stream from image 1."));
        CsAcqCfg.u32Mode |= CS_MODE_USER1;
    }
    else if ((i64ExtendedOptions >> 32) & CS_BBOPTIONS_STREAM)
    {
        _ftprintf(stdout, _T("\nSelecting Expert Stream from image 2."));
        CsAcqCfg.u32Mode |= CS_MODE_USER2;
    }
    else
    {
        _ftprintf(stdout, _T("\nCurrent system does not support Expert Stream."));
        _ftprintf(stdout, _T("\nApplication terminated."));
        return CS_MISC_ERROR;
    }

    // Sets the Acquisition values down the driver, without any validation,
    // for the Commit step which will validate system configuration.
    i32Status = CsSet(hSystem, CS_ACQUISITION, &CsAcqCfg);
    if (CS_FAILED(i32Status))
    {
        DisplayErrorString(i32Status);
        return CS_MISC_ERROR;
    }

    return CS_SUCCESS; // Success
}

void gageControlThread::UpdateProgress( DWORD	dwTickStart, uInt32 u32UpdateInterval_ms, unsigned long long llSamples )
{
    static	DWORD	dwTickLastUpdate = 0;
    uInt32	h, m, s;
    DWORD	dwElapsed;
    DWORD	dwTickNow = GetTickCount();


    if ((dwTickNow - dwTickLastUpdate > u32UpdateInterval_ms))
    {
        double dRate;

        dwTickLastUpdate = dwTickNow;
        dwElapsed = dwTickNow - dwTickStart;

        if (dwElapsed)
        {
            dRate = (llSamples / 1000000.0) / (dwElapsed / 1000.0);
            h = m = s = 0;
            if (dwElapsed >= 1000)
            {
                if ((s = dwElapsed / 1000) >= 60)	// Seconds
                {
                    if ((m = s / 60) >= 60)			// Minutes
                    {
                        if ((h = m / 60) > 0)		// Hours
                            m %= 60;
                    }

                    s %= 60;
                }
            }
            printf ("\rTotal data: %0.2f MS, Rate: %6.2f MS/s, Elapsed Time: %u:%02u:%02u  ", llSamples / 1000000.0, dRate, h, m, s);
        }
    }
}

int64 gageControlThread::SumBufferData( void* pBuffer, uInt32 u32Size, uInt32 u32SampleBits )
{
    // In this routine we sum up all the samples in the buffer. This function
    // should be replaced with the user's analysys function
    uInt32 i;
    uInt8* pu8Buffer = NULL;
    int16* pi16Buffer = NULL;

    int64 i64Sum = 0;
    if ( 8 == u32SampleBits )
    {
        pu8Buffer = (uInt8 *)pBuffer;
        for (i = 0; i < u32Size; i++)
        {
            i64Sum += pu8Buffer[i];
        }
    }
    else
    {
        pi16Buffer = (int16 *)pBuffer;
        for (i = 0; i < u32Size; i++)
        {
            i64Sum += pi16Buffer[i];
        }
    }
    return i64Sum;
}

void gageControlThread::SaveResults(int16* saveData)
{
//    pFile = _tfopen((const wchar_t*)strFileName, (const wchar_t*)"w");
//    _ftprintf(pFile, _T("Loop\tSum\n\n"));
//    for (i = 0; i < u32Count; i++)
//    {
//        _ftprintf(pFile, _T("  %u\t%I64d\n"), i+1, pi64Results[i]);
//    }
//    fclose(pFile);
}

int32 gageControlThread::LoadStmConfiguration(LPCTSTR szIniFile, PCSSTMCONFIG pConfig)
{
    TCHAR	szDefault[MAX_PATH];
    TCHAR	szString[MAX_PATH];
    TCHAR	szFilePath[MAX_PATH];
    int		nDummy;

    CSSTMCONFIG CsStmCfg;

    // Set defaults in case we can't read the ini file
    CsStmCfg.u32TransferTimeout = TRANSFER_TIMEOUT;
    //strcpy((char*)CsStmCfg.strResultFile, (char*)_T(OUT_FILE));

    if (NULL == pConfig)
    {
        return (CS_INVALID_PARAMETER);
    }

    GetFullPathName(szIniFile, MAX_PATH, szFilePath, NULL);

    if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(szFilePath))
    {
        *pConfig = CsStmCfg;
        return (CS_USING_DEFAULTS);
    }

    if (0 == GetPrivateProfileSection(STM_SECTION, szString, 100, szFilePath))
    {
        *pConfig = CsStmCfg;
        return (CS_USING_DEFAULTS);
    }

    nDummy = 0;
    CsStmCfg.bDoAnalysis = (0 != GetPrivateProfileInt(STM_SECTION, _T("DoAnalysis"), nDummy, szFilePath));

    nDummy = CsStmCfg.u32TransferTimeout;
    CsStmCfg.u32TransferTimeout = GetPrivateProfileInt(STM_SECTION, _T("TimeoutOnTransfer"), nDummy, szFilePath);

    _stprintf(szDefault, _T("%s"), CsStmCfg.strResultFile);
    GetPrivateProfileString(STM_SECTION, _T("ResultsFile"), szDefault, szString, MAX_PATH, szFilePath);
    _tcscpy(CsStmCfg.strResultFile, szString);

    *pConfig = CsStmCfg;
    return (CS_SUCCESS);
}
