#include "injection.h"
#include <stdio.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

DWORD ProcesstoPid(char *Processname);
ULONG_PTR FindImageBase(HANDLE hProc, LPSTR lpCommandline);

int main(int argc, char *argv[])
{
    // usage: ./DllInjector.exe targetFile dllName
    int mode = 2;
    //printf("����ע��ģʽ[1-3]:");
    //scanf("%d", &mode);
    //mode--;

    BOOL injResult;
    switch (mode)
    {
        case INJ_ORIGIN:
            //injResult = InjAtOriginalSection(argv[1], argv[2]);
            break;
        case INJ_EXTEND:
            //injResult = InjByExtendingLastSection(argv[1], argv[2]);
            break;
        case INJ_ADD:
            injResult = InjByAddingNewSection(argv[1], argv[2], ".inj");
            break;
    default:
        printf("��֧�ֵ�ע��ģʽ\n");
        return FALSE;
    }

    return 0;
}