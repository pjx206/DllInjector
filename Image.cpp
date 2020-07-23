#include "Image.h"
#include <Shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "shlwapi.lib")

CImage::CImage()
{
    m_hFile = INVALID_HANDLE_VALUE;
    m_hModule = NULL;
    m_pDosHeader = NULL;
    m_pFileHeader = NULL;
    m_pRelocTable = NULL;
    m_pSecHeader = NULL;
    m_pExportDir = NULL;
    m_pImportDesp = NULL;
    m_pOptHeader = NULL;

    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    m_dwPageSize = sysinfo.dwPageSize;
}

CImage::~CImage()
{
    Cleanup();
}

PBYTE CImage::LoadImage(HANDLE hFile, BOOL bDoReloc, ULONG_PTR RelocBase, BOOL bDoImport)
{
    DWORD dwFileSize = 0;
    BOOL bResult = FALSE;
    DWORD dwNumOfBytesRead;
    PIMAGE_SECTION_HEADER pTmpSecHeader = NULL;
    BYTE *pMemory = NULL;
    __try
    {
        m_hFile = hFile;
        dwFileSize = GetFileSize(m_hFile, NULL);
        if (dwFileSize == 0)
        {
            strcpy(m_szErrorMsg, "�ļ���СΪ0!"); // TODO: ��FormatErrorMsg()�ع��£��������ִ�����Ϣ���췽ʽ
            __leave;
        }

        DWORD dwSizeToRead = (dwFileSize > PEHEADER_SIZE) ? PEHEADER_SIZE : dwFileSize;
        ZeroMemory(m_HeaderData, PEHEADER_SIZE);
        bResult = ReadFile(m_hFile, m_HeaderData, dwSizeToRead, &dwNumOfBytesRead, NULL);
        if (!bResult)
        {
            FormatErrorMsg("��ȡ�ļ�ʧ��", GetLastError());
            __leave;
        }

        if (!VerifyImage(m_HeaderData))
        {
            strcpy(m_szErrorMsg, "������Ч��PEӳ��");
            __leave;
        }

        InitializePEHeaders(m_HeaderData);

        pTmpSecHeader = m_pSecHeader;

        pMemory = m_hModule = (BYTE *)VirtualAlloc(NULL, m_TotalImageSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (m_hModule == NULL)
        {
            strcpy(m_szErrorMsg, "�ڴ治�㣬Ϊ����PEͷ���������ڴ�ʧ��");
            __leave;
        }

        memcpy(pMemory, m_HeaderData, m_pOptHeader->SizeOfHeaders);
        pMemory += GetAlignedSize(m_pOptHeader->SizeOfHeaders, m_pOptHeader->SectionAlignment);

        LARGE_INTEGER liFileOffset;
        for (int i = 0; i < m_SectionCnt; i++)
        {
            liFileOffset.QuadPart = pTmpSecHeader->PointerToRawData;
            bResult = SetFilePointerEx(m_hFile, liFileOffset, NULL, FILE_BEGIN);
            if (!bResult)
            {
                FormatErrorMsg("�����ļ���дλ��ʧ��!", GetLastError());
                __leave;
            }

            //��ȡ������
            bResult = ReadFile(m_hFile, pMemory, pTmpSecHeader->SizeOfRawData, &dwNumOfBytesRead, NULL);
            if (!bResult)
            {
                FormatErrorMsg("��ȡ�ļ�ʧ��!", GetLastError());
                __leave;
            }
            pMemory += GetAlignedSize(pTmpSecHeader->Misc.VirtualSize, m_pOptHeader->SectionAlignment);
            pTmpSecHeader++;
        }

        //���½���PEͷ
        InitializePEHeaders(m_hModule);

        //��ʼ�����ض�λ����
        if (bDoReloc)
        {
            //���RelocBaseΪ0����ʵ�ʼ���λ�ý����ض�λ
            ULONG_PTR BaseToReloc = (RelocBase == 0) ? (DWORD)m_hModule : RelocBase;
            ProcessRelocTable(BaseToReloc);
        }

        //�������
        if (bDoImport)
        {
            ProcessImportTable();
        }

        bResult = TRUE; //���سɹ�
    }
    __finally
    {
        if (!bResult)
        {
            if (m_hFile != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_hFile);
                m_hFile = INVALID_HANDLE_VALUE;
            }
        }
    }

    return m_hModule;
}

//���ļ�·����ʽ��PE
PBYTE CImage::LoadImage(char *szPEPath, BOOL bDoReloc, ULONG_PTR RelocBase, BOOL bDoImport)
{
    //����PE·��
    strcpy(m_szPEPath, szPEPath);
    //��ֻ����ʽ���ļ�
    m_hFile = CreateFileA(szPEPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == INVALID_HANDLE_VALUE)
    {
        FormatErrorMsg("���ļ�ʧ��!", GetLastError());
        return NULL;
    }

    return LoadImage(m_hFile, bDoReloc, RelocBase, bDoImport);
}

VOID CImage::FreePE()
{
    VirtualFree(m_hModule, 0, MEM_RELEASE);
    m_hModule = NULL;
}

DWORD CImage::GetAlignedSize(DWORD theSize, DWORD Alignment)
{
    // �ѻ�ȡ�����С�ĺ��װ��CImage����
    DWORD dwAlignedVirtualSize = 0;
    dwAlignedVirtualSize = ALIGN_SIZE_UP(theSize, Alignment);
    return dwAlignedVirtualSize; //���ض����Ĵ�С
}

ULONG_PTR CImage::GetAlignedPointer(ULONG_PTR uPointer, DWORD Alignment)
{
    DWORD dwAlignedAddress = 0;
    dwAlignedAddress = ALIGN_SIZE_UP(uPointer, Alignment);
    return dwAlignedAddress; //���ض����Ĵ�С
}

DWORD CImage::_GetProcAddress(PBYTE pModule, char *szFuncName)
{
    //�Լ�ʵ��GetProcAddress
    DWORD retAddr = 0;
    DWORD *namerav, *funrav;
    DWORD cnt = 0;
    DWORD max, min, mid;
    WORD *nameOrdinal;
    WORD nIndex = 0;
    int cmpresult = 0;
    char *ModuleBase = (char *)pModule;
    char *szMidName = NULL;
    PIMAGE_DOS_HEADER pDosHeader;
    PIMAGE_NT_HEADERS pNtHeader;
    PIMAGE_OPTIONAL_HEADER pOptHeader;
    PIMAGE_EXPORT_DIRECTORY pExportDir;

    if (ModuleBase == NULL)
    {
        return 0;
    }

    pDosHeader = (PIMAGE_DOS_HEADER)ModuleBase;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return 0;
    }
    pNtHeader = (PIMAGE_NT_HEADERS)(ModuleBase + pDosHeader->e_lfanew);
    if (pNtHeader->Signature != IMAGE_NT_SIGNATURE)
    {
        return 0;
    }

    pOptHeader = &pNtHeader->OptionalHeader;
    pExportDir = (PIMAGE_EXPORT_DIRECTORY)(ModuleBase + pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    namerav = (DWORD *)(ModuleBase + pExportDir->AddressOfNames);
    funrav = (DWORD *)(ModuleBase + pExportDir->AddressOfFunctions);
    nameOrdinal = (WORD *)(ModuleBase + pExportDir->AddressOfNameOrdinals);

    if ((DWORD)szFuncName < 0x0000FFFF)
    {
        retAddr = (DWORD)(ModuleBase + funrav[(WORD)szFuncName]);
    }
    else
    {
        //���ַ�����
        max = pExportDir->NumberOfNames;
        min = 0;
        mid = (max + min) / 2;
        while (min < max)
        {
            szMidName = ModuleBase + namerav[mid];
            cmpresult = strcmp(szFuncName, szMidName);
            if (cmpresult < 0)
            {
                //����ֵС,��ȡ��ֵ-1Ϊ���ֵ
                max = mid - 1;
            }
            else if (cmpresult > 0)
            {
                //����ֵ��,��ȡ��ֵ+1Ϊ��Сֵ
                min = mid + 1;
            }
            else
            {
                break;
            }
            mid = (max + min) / 2;
        }

        if (strcmp(szFuncName, ModuleBase + namerav[mid]) == 0)
        {
            nIndex = nameOrdinal[mid];
            retAddr = (DWORD)(ModuleBase + funrav[nIndex]);
        }
    }
    return retAddr;
}

DWORD CImage::GetTotalImageSize(DWORD Alignment)
{
    // Alignment�����£�ӳ���С
    DWORD TotalSize = 0;
    DWORD tmp = 0;
    PIMAGE_SECTION_HEADER pTmpSecHeader = m_pSecHeader;
    TotalSize += GetAlignedSize(m_pOptHeader->SizeOfHeaders, Alignment);
    for (WORD i = 0; i < m_SectionCnt; i++)
    {
        tmp = GetAlignedSize(pTmpSecHeader->Misc.VirtualSize, Alignment);
        TotalSize += tmp;
        pTmpSecHeader++;
    }
    return TotalSize;
}

// ��ַ����
DWORD CImage::Rva2Raw(DWORD VirtualAddr)
{
    DWORD RawAddr = 0;
    if (VirtualAddr < m_pOptHeader->SizeOfHeaders)
    {
        RawAddr = VirtualAddr;
        return RawAddr;
    }
    PIMAGE_SECTION_HEADER pTmpSecHeader = LocateSectionByRVA(VirtualAddr);
    if (pTmpSecHeader != NULL)
    {
        RawAddr = VirtualAddr - pTmpSecHeader->VirtualAddress + pTmpSecHeader->PointerToRawData;
    }

    return RawAddr;
}

DWORD CImage::Raw2Rva(DWORD RawAddr)
{
    DWORD RvaAddr = 0;
    if (RawAddr < m_pOptHeader->SizeOfHeaders)
    {
        RvaAddr = RawAddr;
        return RvaAddr;
    }
    PIMAGE_SECTION_HEADER pTmpSecHeader = LocateSectionByRawOffset(RawAddr);
    if (pTmpSecHeader != NULL)
    {
        RvaAddr = RawAddr - pTmpSecHeader->PointerToRawData + pTmpSecHeader->VirtualAddress;
    }

    return RvaAddr;
}

VOID CImage::FormatErrorMsg(char *szPrompt, DWORD ErrCode)
{
    // �д��Ľ�
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        ErrCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPTSTR)&lpMsgBuf,
        0,
        NULL);
    sprintf(m_szErrorMsg, "%s �������:%d ԭ��:%s", szPrompt, ErrCode, (LPCTSTR)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

VOID CImage::Cleanup()
{
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }

    if (m_hModule != NULL)
    {
        FreePE();
    }
}

VOID CImage::InitializePEHeaders(PBYTE pBase)
{
    //��������PEͷ���ṹ
    m_hModule = pBase;
    m_pDosHeader = (PIMAGE_DOS_HEADER)pBase;
    m_pNtHeaders = (PIMAGE_NT_HEADERS)(pBase + m_pDosHeader->e_lfanew);
    m_pFileHeader = &m_pNtHeaders->FileHeader;
    m_SectionCnt = m_pFileHeader->NumberOfSections;
    m_pOptHeader = &m_pNtHeaders->OptionalHeader;
    m_pRelocTable = &(m_pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]);
    m_pSecHeader = (PIMAGE_SECTION_HEADER)((BYTE *)m_pOptHeader + sizeof(IMAGE_OPTIONAL_HEADER));
    m_dwEntryPoint = m_pOptHeader->AddressOfEntryPoint;
    m_TotalImageSize = m_pOptHeader->SizeOfImage;
    m_ImageBase = (ULONG_PTR)m_pOptHeader->ImageBase;

    // �����
    m_pImpDataDir = &m_pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    // ����ԭ�����
    m_OldImpDir.VirtualAddress = m_pImpDataDir->VirtualAddress;
    m_OldImpDir.Size = m_pImpDataDir->Size;
    if (m_pImpDataDir->VirtualAddress != NULL)
    {
        m_pImportDesp = (PIMAGE_IMPORT_DESCRIPTOR)(pBase + m_pImpDataDir->VirtualAddress);
    }

    // ������
    m_pExpDataDir = &m_pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (m_pExpDataDir->VirtualAddress != NULL)
    {
        m_pExportDir = (PIMAGE_EXPORT_DIRECTORY)(pBase + m_pExpDataDir->VirtualAddress);
    }
}

VOID CImage::ProcessRelocTable(ULONG_PTR RelocBase)
{
    // NOTE: dllע������к���û���õ�??
    WORD i = 0;
    PIMAGE_BASE_RELOCATION pRelocBlock = NULL;
    if (m_pRelocTable->VirtualAddress != NULL)
    {
        pRelocBlock = (PIMAGE_BASE_RELOCATION)(m_hModule + m_pRelocTable->VirtualAddress);
        //printf("After Loaded,Reloc Table=0x%08X\n",pRelocBlock);
        do
        { //����һ����һ�����ض�λ�飬���һ���ض�λ����RAV=0����
            //��Ҫ�ض�λ�ĸ������Ǳ���Ĵ�С��ȥ��ͷ�Ĵ�С���������DWORD��ʾ�Ĵ�С
            //���ض�λ������16λ�ģ��Ǿ͵ó���2
            int numofReloc = (pRelocBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            //printf("Reloc Data num=%d\n",numofReloc);
            //�ض�λ������16λ��
            WORD offset = 0;
            WORD *pRelocData = (WORD *)((BYTE *)pRelocBlock + sizeof(IMAGE_BASE_RELOCATION));
            for (i = 0; i < numofReloc; i++) //ѭ������ֱ���ж�*pData�Ƿ�Ϊ0Ҳ������Ϊ�������
            {
                ULONG_PTR *RelocAddress = 0; //��Ҫ�ض�λ�ĵ�ַ
#ifdef _WIN64
                WORD RelocFlag = IMAGE_REL_BASED_DIR64;
#else
                WORD RelocFlag = IMAGE_REL_BASED_HIGHLOW;
#endif
                //IMAGE_REL_BASED_DIR64
                //�ض�λ�ĸ�4λ���ض�λ���ͣ�
                if (((*pRelocData) >> 12) == RelocFlag) //�ж��ض�λ�����Ƿ�ΪIMAGE_REL_BASED_HIGHLOW,x86
                {
                    //������Ҫ�����ض�λ�ĵ�ַ
                    //�ض�λ���ݵĵ�12λ�ټ��ϱ��ض�λ��ͷ��RAV��������Ҫ�ض�λ�����ݵ�RAV
                    offset = (*pRelocData) & 0xFFF; //Сƫ��
                    RelocAddress = (ULONG_PTR *)(m_hModule + pRelocBlock->VirtualAddress + offset);
                    //����Ҫ�ض�λ�����ݽ�������
                    //��������:��ȥIMAGE_OPTINAL_HEADER�еĻ�ַ���ټ����µĻ�ַ����
                    *RelocAddress = *RelocAddress - m_pOptHeader->ImageBase + RelocBase;
                }
                pRelocData++;
            }
            //ָ����һ���ض�λ��
            pRelocBlock = (PIMAGE_BASE_RELOCATION)((char *)pRelocBlock + pRelocBlock->SizeOfBlock);

        } while (pRelocBlock->VirtualAddress);
    }
}

BOOL CImage::ProcessImportTable()
{
    BOOL bResult = TRUE;
    char szPreDirectory[MAX_PATH] = {0};
    char szCurDirectory[MAX_PATH] = {0};
    char szPrompt[256] = {0};
    PIMAGE_IMPORT_DESCRIPTOR pImportDescriptor = m_pImportDesp;
    PIMAGE_THUNK_DATA NameThunk = NULL, AddrThunk = NULL;
    PIMAGE_IMPORT_BY_NAME pImpName = NULL;
    char *szImpModName = NULL;

    if (pImportDescriptor == NULL)
    {
        //�޵��������Ҫ����
        return TRUE;
    }

    //���ĵ�ǰ·����m_szPEPath���������ĳЩ������dllʱ���Ҳ���ģ��
    GetCurrentDirectoryA(MAX_PATH, szPreDirectory);
    strcpy(szCurDirectory, m_szPEPath);
    PathRemoveFileSpecA(szCurDirectory);
    SetCurrentDirectoryA(szCurDirectory);

    // ����ÿ��dll
    while (pImportDescriptor->Name && pImportDescriptor->OriginalFirstThunk)
    {
        szImpModName = (char *)m_hModule + pImportDescriptor->Name;
        HMODULE hMod = LoadLibraryA(szImpModName); // ����������dll
        if (hMod == NULL)
        {
            sprintf(szPrompt, "���ص����ģ�� %s ʧ��!", szImpModName);
            FormatErrorMsg(szImpModName, GetLastError());
            return FALSE;
        }

        NameThunk = (PIMAGE_THUNK_DATA)(m_hModule + (ULONG)pImportDescriptor->OriginalFirstThunk);
        AddrThunk = (PIMAGE_THUNK_DATA)(m_hModule + (ULONG)pImportDescriptor->FirstThunk);

        // ����Thunk��ת��IAT
        while (NameThunk->u1.AddressOfData)
        {
            bResult = SnapThunk(hMod, szImpModName, m_hModule, NameThunk, AddrThunk);
            if (!bResult)
            {
                bResult = FALSE;
                break;
            }
            NameThunk++;
            AddrThunk++;
        }

        if (!bResult)
        {
            break;
        }
        pImportDescriptor++;
    }

    // �ָ����̵�Ŀ¼
    SetCurrentDirectoryA(szPreDirectory);
    return bResult;
}

//NOTE: ��INTת�� IAT��
BOOL CImage::SnapThunk(HMODULE hImpMode, char *szImpModeName, PBYTE ImageBase, PIMAGE_THUNK_DATA NameThunk, PIMAGE_THUNK_DATA AddrThunk)
{
    BOOL bResult = FALSE;
    PIMAGE_IMPORT_BY_NAME pImpName = NULL;
    DWORD dwFunAddr = 0;
    ULONG Ordinal = 0;

    // �������λȷ������Ż�������
    // TODO: ���ﶼ������64λ�ģ���Ȼ����ŵ�����������.......
    if (NameThunk->u1.AddressOfData & IMAGE_ORDINAL_FLAG32)
    {
        Ordinal = IMAGE_ORDINAL(NameThunk->u1.Ordinal);
        dwFunAddr = (DWORD)GetProcAddress(hImpMode, (LPCSTR)Ordinal);
        if (dwFunAddr == 0)
        {
            sprintf(m_szErrorMsg, "�޷��ڵ���ģ��%s�ж�λ���뺯��:%d (���)", szImpModeName, Ordinal);
        }
    }
    else
    {
        pImpName = (PIMAGE_IMPORT_BY_NAME)(m_hModule + (ULONG)NameThunk->u1.AddressOfData);
        dwFunAddr = (DWORD)GetProcAddress(hImpMode, (LPCSTR)pImpName->Name);
        //printf("0x%08X �����Ƶ��� : %s\n",dwFunAddr,pImpName->Name);
        if (dwFunAddr == 0)
        {
            sprintf(m_szErrorMsg, "�޷��ڵ���ģ��%s�ж�λ���뺯��:%s ", szImpModeName, pImpName->Name);
        }
    }

    if (dwFunAddr != 0)
    {
        AddrThunk->u1.Function = dwFunAddr;
        bResult = TRUE;
    }

    return bResult;
}

BOOL CImage::VerifyImage(PVOID pBase)
{
    m_pDosHeader = (PIMAGE_DOS_HEADER)pBase;
    if (m_pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return FALSE;
    }
    m_pNtHeaders = (PIMAGE_NT_HEADERS)((PBYTE *)pBase + m_pDosHeader->e_lfanew);
    if (m_pNtHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return FALSE;
    }
    return TRUE;
}

LPSTR CImage::GetErrorMsg(char *szBuf, int BufSize)
{
    int len = strlen(m_szErrorMsg);
    if (len <= BufSize)
    {
        strcpy(szBuf, m_szErrorMsg);
        return szBuf;
    }
    return NULL;
}

PIMAGE_SECTION_HEADER CImage::LocateSectionByRVA(DWORD dwRVA)
{
    PIMAGE_SECTION_HEADER pTemp = m_pSecHeader;
    for (int i = 0; i < m_SectionCnt; i++)
    {
        if (pTemp->VirtualAddress <= dwRVA &&
            dwRVA < (pTemp->VirtualAddress + pTemp->Misc.VirtualSize))
        {
            return pTemp;
        }
        pTemp++;
    }
    return NULL;
}

PIMAGE_SECTION_HEADER CImage::LocateSectionByRawOffset(DWORD dwRawOffset)
{
    PIMAGE_SECTION_HEADER pTemp = m_pSecHeader;
    for (int i = 0; i < m_SectionCnt; i++)
    {
        if (pTemp->PointerToRawData <= dwRawOffset && dwRawOffset < (pTemp->PointerToRawData + pTemp->SizeOfRawData))
        {
            return pTemp;
        }
        pTemp++;
    }
    return NULL;
}

DWORD CImage::GetSectionVirtualPaddingSize(PIMAGE_SECTION_HEADER pSecHeader)
{
    DWORD AlignedSize = GetAlignedSize(pSecHeader->Misc.VirtualSize, m_pOptHeader->SectionAlignment);
    return AlignedSize - pSecHeader->Misc.VirtualSize;
}

DWORD CImage::GetSectionPhysialPaddingSize(PIMAGE_SECTION_HEADER pSecHeader)
{
    DWORD dwPaddingSize = 0;
    if (pSecHeader->Misc.VirtualSize < pSecHeader->SizeOfRawData)
    {
        dwPaddingSize = pSecHeader->SizeOfRawData - pSecHeader->Misc.VirtualSize;
    }
    else
    {
        // ������С�����ļ��ýڴ�С������Ϊû��϶
        dwPaddingSize = 0;
    }
    return dwPaddingSize;
}

BOOL CImage::MakeFileHandleWritable()
{
    BOOL bResult = FALSE;
    HANDLE hNew = INVALID_HANDLE_VALUE;
    HANDLE hProc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, GetCurrentProcessId());
    if (hProc == NULL)
    {
        return FALSE;
    }
    bResult = DuplicateHandle(
        hProc,
        m_hFile,
        hProc,
        &hNew,
        GENERIC_READ,
        FALSE,
        0);

    if (bResult)
    {
        CloseHandle(m_hFile);
        m_hFile = hNew;
    }
    else
    {
        FormatErrorMsg("���ľ��Ȩ��ʧ��", GetLastError());
    }

    CloseHandle(hProc);
    return bResult;
}

VOID CImage::AttachToMemory(PVOID pMemory)
{
    if (pMemory != NULL)
    {
        InitializePEHeaders((PBYTE)pMemory);
    }
}

BOOL CImage::AttachToProcess(HANDLE hProc, PVOID ProcessImageBase)
{
    BOOL bResult = FALSE;
    SIZE_T dwNumOfBytes = 0;
    m_hProc = hProc;
    m_ImageBase = (ULONG_PTR)ProcessImageBase;
    bResult = ReadProcessMemory(m_hProc, (LPVOID)m_ImageBase, m_HeaderData, 0x1000, &dwNumOfBytes);
    if (!bResult)
    {
        FormatErrorMsg("ReadProcessMemoryʧ�ܣ�", GetLastError());
        return FALSE;
    }

    InitializePEHeaders(m_HeaderData);
    return bResult;
}

PIMAGE_SECTION_HEADER CImage::AddNewSectionToFile(char *szSectionName, DWORD SectionSize)
{
    PIMAGE_SECTION_HEADER pNewSecHeader = m_pSecHeader + m_SectionCnt;
    PIMAGE_SECTION_HEADER pLastSecHeader = m_pSecHeader + m_SectionCnt - 1;
    DWORD dwSectionVA, dwSectionRawOffset, dwSectionSize;

    LARGE_INTEGER liFileOffset;
    BOOL bResult;
    DWORD dwNumOfBytes;

    dwSectionVA = pLastSecHeader->VirtualAddress + GetAlignedSize(pLastSecHeader->Misc.VirtualSize, m_pOptHeader->SectionAlignment);
    dwSectionRawOffset = pLastSecHeader->PointerToRawData + GetAlignedPointer(pLastSecHeader->SizeOfRawData, m_pOptHeader->FileAlignment);
    dwSectionSize = GetAlignedSize(SectionSize, m_pOptHeader->FileAlignment);

    liFileOffset.QuadPart = dwSectionRawOffset + dwSectionSize;
    bResult = SetFilePointerEx(m_hFile, liFileOffset, NULL, FILE_BEGIN);
    if (!bResult)
    {
        FormatErrorMsg("����½�ʱ�����ļ�ָ�����", GetLastError());
        return NULL;
    }
    bResult = SetEndOfFile(m_hFile);
    if (!bResult)
    {
        FormatErrorMsg("����½�ʱ�����ļ�����λ�ô���!", GetLastError());
        return NULL;
    }

    // ����½ڵ�SectionHeader
    ZeroMemory(pNewSecHeader, sizeof(IMAGE_SECTION_HEADER));
    strncpy((char *)pNewSecHeader->Name, szSectionName, 8);
    pNewSecHeader->Misc.VirtualSize = dwSectionSize;
    pNewSecHeader->VirtualAddress = dwSectionVA;
    pNewSecHeader->PointerToRawData = dwSectionRawOffset;
    pNewSecHeader->SizeOfRawData = dwSectionSize;
    pNewSecHeader->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;

    m_pFileHeader->NumberOfSections += 1;
    m_SectionCnt++;

    m_pOptHeader->SizeOfImage += dwSectionSize;

    liFileOffset.QuadPart = 0;
    bResult = SetFilePointerEx(m_hFile, liFileOffset, NULL, FILE_BEGIN);
    if (!bResult)
    {
        FormatErrorMsg("����½ڱ���PEʱ�����ļ�ָ�����!", GetLastError());
        return NULL;
    }

    bResult = WriteFile(m_hFile, m_hModule, m_pOptHeader->SizeOfHeaders, &dwNumOfBytes, NULL);
    if (!bResult)
    {
        FormatErrorMsg("����½ڱ���PEʱд���ļ�����!", GetLastError());
        return NULL;
    }

    FlushFileBuffers(m_hFile);
    return pNewSecHeader;
}

PIMAGE_SECTION_HEADER CImage::AddNewSectionToMemory(char *szSectionName, DWORD SectionSize)
{
    PIMAGE_SECTION_HEADER pNewSecHeader = m_pSecHeader + m_SectionCnt;
    PIMAGE_SECTION_HEADER pLastSecHeader = m_pSecHeader + m_SectionCnt - 1;
    DWORD dwSectionVA, dwSectionRawOffset, dwSectionSize;
    BOOL bResult = FALSE;
    SIZE_T dwIoCnt = 0;

    HANDLE hProc = (m_hProc == NULL) ? GetCurrentProcess() : m_hProc; // TODO: GetCurrentProcess���ã���
    ULONG_PTR HighestUserAddress = 0;
    BYTE PEHeader[0x1000] = {0};

    SYSTEM_INFO sysinfo = {0};
    GetSystemInfo(&sysinfo);
    HighestUserAddress = (ULONG_PTR)sysinfo.lpMaximumApplicationAddress;

    dwSectionVA = pLastSecHeader->VirtualAddress + ALIGN_SIZE_UP(pLastSecHeader->Misc.VirtualSize, m_pOptHeader->SectionAlignment);
    dwSectionRawOffset = pLastSecHeader->PointerToRawData + GetAlignedSize(pLastSecHeader->SizeOfRawData, m_pOptHeader->FileAlignment);
    dwSectionSize = GetAlignedSize(SectionSize, m_pOptHeader->FileAlignment);

    ULONG_PTR dwNewSectionStartAddr = m_ImageBase + dwSectionVA;
    ULONG_PTR AddressToAlloc = GetAlignedPointer(dwNewSectionStartAddr, sysinfo.dwAllocationGranularity);
    PBYTE AllocatedMem = NULL;
    for (AddressToAlloc = dwNewSectionStartAddr; AddressToAlloc < HighestUserAddress; AddressToAlloc += sysinfo.dwAllocationGranularity)
    {
        // TODO: �ٿ���VirtualAllocEx���ĵ�
        AllocatedMem = (PBYTE)VirtualAllocEx(hProc, (PVOID)AddressToAlloc, dwSectionSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (AllocatedMem != NULL)
        {
            break;
        }
    }

    if (AllocatedMem == NULL)
    {
        FormatErrorMsg("����½�ʱ��Ŀ������������ڴ�ʧ��!", GetLastError());
        return NULL;
    }

    dwSectionVA = MEM_OFFSET(AllocatedMem, m_ImageBase);

    ZeroMemory(pNewSecHeader, sizeof(IMAGE_SECTION_HEADER));
    strncpy((char *)pNewSecHeader->Name, szSectionName, 8);
    pNewSecHeader->Misc.VirtualSize = dwSectionSize;
    pNewSecHeader->VirtualAddress = dwSectionVA;
    pNewSecHeader->PointerToRawData = dwSectionRawOffset;
    pNewSecHeader->SizeOfRawData = dwSectionSize;
    pNewSecHeader->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;

    //����PEͷ�еĽڸ���
    m_pFileHeader->NumberOfSections += 1;
    m_SectionCnt++;
    //����PEͷ�е���ӳ���С
    m_pOptHeader->SizeOfImage += dwSectionSize;

    DWORD dwOldProtect = 0;
    bResult = VirtualProtectEx(hProc, (LPVOID)m_ImageBase, m_pOptHeader->SizeOfHeaders, PAGE_READWRITE, &dwOldProtect);
    if (!bResult)
    {
        FormatErrorMsg("�޸�Ŀ������ڴ�����ʱʧ��!", GetLastError());
        return NULL;
    }

    bResult = WriteProcessMemory(hProc, (LPVOID)m_ImageBase, m_HeaderData, m_pOptHeader->SizeOfHeaders, &dwIoCnt);
    if (!bResult)
    {
        FormatErrorMsg("��Ŀ�����д��PEͷ����ʱ����!", GetLastError());
        return NULL;
    }

    return pNewSecHeader;
}

PIMAGE_SECTION_HEADER CImage::ExtraLastSectionSizeToFile(DWORD SectionAddSize)
{
	PIMAGE_SECTION_HEADER pLastSecHeader = m_pSecHeader + m_SectionCnt  - 1;
	DWORD dwSectionNewVirtualSize,dwSectionNewRawOffset,dwSectionNewRawSize;
	DWORD dwOldSectionVirtualSize = 0 ;
	LARGE_INTEGER liFileOffset;
	BOOL bResult = FALSE ;
	DWORD dwIoCnt = 0 ;
	
	//����չ���һ���ڵ�����£���Ҫ�������һ���ڵ�RawSize��VirtualSize����ʼƫ�ƾ�����
	//�����½ڵ������С�����ļ��������ȶ���
	dwSectionNewRawOffset = pLastSecHeader->PointerToRawData ;
	dwSectionNewRawSize = GetAlignedSize(pLastSecHeader->SizeOfRawData + SectionAddSize,m_pOptHeader->FileAlignment);
	dwOldSectionVirtualSize = dwSectionNewVirtualSize =  GetAlignedSize(pLastSecHeader->Misc.VirtualSize , m_pOptHeader->SectionAlignment);
	//�ƴ��½ڵ�VirtualSize��С,�����ڴ��СС���ļ���Сʱ����Ҫ����
	if (pLastSecHeader->Misc.VirtualSize < dwSectionNewRawSize)
	{
		dwSectionNewVirtualSize += SectionAddSize;
	}
	
	//�����ļ�ָ��λ��
	liFileOffset.QuadPart = dwSectionNewRawOffset +  pLastSecHeader->SizeOfRawData + SectionAddSize; //TODOL�����ļ����룿
	bResult = SetFilePointerEx(m_hFile,liFileOffset,NULL,FILE_BEGIN);
	if (!bResult)
	{
		FormatErrorMsg("����½�ʱ�����ļ�ָ�����!",GetLastError());
		return NULL;
		
	}
	
	bResult = SetEndOfFile(m_hFile);
	if (!bResult)
	{
		FormatErrorMsg("����½�ʱ�����ļ�����λ�ô���!",GetLastError());
		return NULL;
		
	}
	
	//���SectionHeader
	pLastSecHeader->Misc.VirtualSize = dwSectionNewVirtualSize;
	pLastSecHeader->SizeOfRawData = dwSectionNewRawSize;
	pLastSecHeader->Characteristics |=  IMAGE_SCN_MEM_READ;
	
	//����PEͷ�е���ӳ���С
	m_pOptHeader->SizeOfImage = m_pOptHeader->SizeOfImage - dwOldSectionVirtualSize 
		+ GetAlignedSize(pLastSecHeader->Misc.VirtualSize,m_pOptHeader->SectionAlignment);
	
	//����PEͷ���ļ���
	liFileOffset.QuadPart = 0;
	bResult = SetFilePointerEx(m_hFile,liFileOffset,NULL,FILE_BEGIN);
	if (!bResult)
	{
		FormatErrorMsg("����½ڱ���PEʱ�����ļ�ָ�����!",GetLastError());
		return NULL;
		
	}
	
	bResult = WriteFile(m_hFile,m_hModule,m_pOptHeader->SizeOfHeaders,&dwIoCnt,NULL);
	if (!bResult)
	{
		FormatErrorMsg("����½ڱ���PEʱд���ļ�����!",GetLastError());
		return NULL;
		
	}
	
	FlushFileBuffers(m_hFile);
	return pLastSecHeader;
	
}