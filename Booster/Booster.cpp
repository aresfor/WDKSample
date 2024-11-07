
//头文件引用顺序很重要
#include <Windows.h>
//公用头文件
#include "../Common.h"
#include "../SysMonCommon.h"

#include <iostream>
#include <stdio.h>

#define SYMBOLLINK_NAME L"\\\\.\\Sample"

int Error(const char* message)
{
    printf("%s (error=%d)\n",message, GetLastError( ));
    return 1;

}

HANDLE OpenDevice()
{
    return CreateFile(SYMBOLLINK_NAME, GENERIC_WRITE | GENERIC_READ
        , FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

int Sample(int argc, const char* argv[])
{
    int result = 0;
    if (argc >= 3)
    {
        printf("Usage: Booster <threadid> <priority>\n");

        HANDLE hDevice = OpenDevice();
        if (hDevice == INVALID_HANDLE_VALUE)
        {
            return Error("Fail to open device\n");
        }

        ThreadData data;
        data.ThreadId = atoi(argv[1]);
        data.Priority = atoi(argv[2]);

        DWORD returnedSize;
        BOOL success = DeviceIoControl(hDevice
            , IOCTRL_SET_PRIORITY, &data, sizeof(data)
            , nullptr, 0, &returnedSize, nullptr);
        if (success)
        {
            printf("Priority Change Success!\n");
        }
        else
        {
            result = Error("Priority Change Failed!\n");
        }

        CloseHandle(hDevice);

        return result;
    }
    else
    {
        Error("Parameter Failed!\n");
        return result;
    }
}

int Zero(LPDWORD readSize, LPDWORD writeSize)
{
    HANDLE hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        return Error("Fail to open device\n");
    }

    BYTE readBuffer[64];
    for (int i = 0; i < sizeof(readBuffer); ++i)
    {
        readBuffer[i] = i+1;
    }

    BOOL readResult = ReadFile(hDevice, readBuffer, sizeof(readBuffer), readSize, nullptr);
    printf("ReadSize: %d\n", *readSize);
    if (!readResult)
    {
        CloseHandle(hDevice);

        return Error("Read Fail!\n");
    }

    if ((*readSize) != sizeof(readBuffer))
    {
        CloseHandle(hDevice);
        return Error("Read Buffer Size Error\n");
    }

    long total =0;
    for (int i = 0; i < sizeof(readBuffer); ++i)
    {
        total += readBuffer[i];
    }
    if (total != 0)
    {
        CloseHandle(hDevice);

        return Error("Read buffer not clear!\n");
    }


    BYTE writeBuffer[1024];
    DWORD originSize = sizeof(writeBuffer);
    BOOL writeResult = WriteFile(hDevice, writeBuffer,originSize, writeSize, nullptr);
    if (!writeResult)
    {
        CloseHandle(hDevice);

        return Error("Write Fail!\n");
    }
    printf("WriteSize: %d\n", *writeSize);

    if ((*writeSize) != originSize)
    {
        CloseHandle(hDevice);

        return Error("Write Buffer Size Error\n");
    }

    CloseHandle(hDevice);

}

void DisplayTime(const LARGE_INTEGER& time)
{
    SYSTEMTIME st;
    FileTimeToSystemTime((FILETIME*) &time, &st);
    printf("%02d:%02d:%02d.%03d  \n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
}


int main(int argc, const char* argv[])
{
    std::cout << "Hello World!\n";

    DWORD readSize = 0, writeSize =0
    , totalRead = 0, totalWrite =0;

    HANDLE hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        return Error("Fail to open device\n");
    }

    BYTE buffer[1 << 16];//64KB

    while (true)
    {
        DWORD readBytes;

        if (false == ReadFile(hDevice, buffer, sizeof(buffer), &readBytes, nullptr))
        {
            return Error("fail read file\n");
        }

        BYTE* pBuffer = buffer;

        DWORD readCount = readBytes;
        std::wstring commandline;
        ProcessExitInfo* exitInfo = nullptr;
        ProcessCreateInfo* createInfo = nullptr;
        if (readCount != 0)
        {
            printf("ReadFile Success, count:%d\n", readBytes);

            while (readCount > 0)
            {
                auto header = (ItemHeader*)pBuffer;
                printf("Read Header Size:%d\n", header->Size);

                switch (header->Type)
                {
                case EItemType::ProcessExit:
                    DisplayTime(header->Time);
                    exitInfo = (ProcessExitInfo*)pBuffer;
                    printf("Process %d Exited\n", exitInfo->ProcessId);
                    break;

                case EItemType::ProcessCreate:
                    DisplayTime(header->Time);
                    createInfo = (ProcessCreateInfo*)pBuffer;
                    //为了正确提取创建进程的命令行，使用wstring宽字符类
                    commandline = std::wstring((WCHAR*)(pBuffer + createInfo->CommandLineOffset)
                        , createInfo->CommandLineLength);
                    printf("Process %d Created, Command line: %ws\n"
                        , createInfo->ProcessId, commandline.c_str());
                    break;

                default:
                    printf("Process Type Error\n");

                    break;
                }

                pBuffer += header->Size;
                readCount -= header->Size;
            }
        }
        else
        {
            //printf("Debug: ReadCount Zero in 200ms\n");

        }

        Sleep(200);
    }



    //while (true)
    //{
    //    //Sample(argc, argv);



    //    /*Zero(&readSize, &writeSize);
    //    totalRead += readSize;
    //    totalWrite += writeSize;
    //    readSize = 0;
    //    writeSize =0;
    //    printf("TotalRead:%d, TotalWrite:%d\n", totalRead, totalWrite);
    //    
    //    */

    //    //getchar();

    //}
    return 0;
}



// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
