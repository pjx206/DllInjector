#include "injection.h"
#include <stdio.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

BOOL InjByAddingNewSection(LPSTR targetFile, LPSTR dllname, LPSTR newSecName)
{
	STARTUPINFOA si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	BYTE HeaderBuffer[0x1000];
	BOOL result = FALSE;

	result = CreateProcessA(
		NULL,
		targetFile, // �ٶ�Ŀ���ļ���һ����ǽ��̴�����commandline
		NULL,
		NULL,
		FALSE,
		//NOTE: �Թ���ʽ�������̣� �����õ���ResumeThread
		CREATE_NEW_CONSOLE | CREATE_SUSPENDED,
		NULL,
		NULL,
		&si,
		&pi);
	if (!result)
	{
		printf("[-] ��������ʧ��\n");
		return FALSE;
	}
	printf("[*] �Ѵ�������\n");

	CImage image;

	// ��ȡ��ַ
	PBYTE imageBase = (PBYTE)FindImageBase(pi.hProcess, targetFile);
	if (imageBase == 0)
	{
		printf("[-] �޷��ҵ�����ӳ���ַ\n");
		return FALSE;
	}
	printf("[*] ����ַ = 0x%p\n", imageBase);

	// ��ȡPEӳ��ͷ��
	if (!image.AttachToProcess(pi.hProcess, imageBase))
	{
		printf("[*] ��ȡĿ������ڴ�ռ�ӳ��ͷ��ʧ��\n");
		return FALSE;
	}
	printf("[*] ��ȡӳ��ͷ�ɹ�\n");
	DWORD oldIIDsSize = image.m_pImpDataDir->Size;
	DWORD oldIIDsCnt = oldIIDsSize / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	DWORD newIIDsCnt = oldIIDsCnt + 1;
	DWORD newIIDsSize = newIIDsCnt * sizeof(IMAGE_IMPORT_DESCRIPTOR);
	printf("[*] ��ǰ�������Ϣ��\n\tVA = 0x%p Size = 0x%X\n",
		image.m_pImpDataDir->VirtualAddress,
		oldIIDsSize);

	PIMAGE_SECTION_HEADER pImpSecHeader = image.LocateSectionByRVA(image.m_pImpDataDir->VirtualAddress);
	printf("[*] ��������ڽ�: %s\tVA = 0x%X\tPointerToRawData = 0x%X\n",
		pImpSecHeader->Name,
		pImpSecHeader->VirtualAddress,
		pImpSecHeader->PointerToRawData);

	// TODO: ��ȡ���뺯���������׼ȷ��thunkDataSize
	char dllExpFunName[] = "Msg"; // NOTE: ͵��������ʵ���Ҫ��ȡһ��dll������dll��������Ϣ���������Ҽٶ�ֻ��һ����������
	DWORD thunkDataSize = sizeof(ULONG_PTR) * 4 + sizeof(WORD) + strlen(dllExpFunName) + 1 + strlen(dllname) + 1;
	// thunkData��ֱ�ӷ���IID�������ģ��������IID���鰴ULONG_PTR����
	DWORD thunkDataOffsetFromNewIIDsEntry = ALIGN_SIZE_UP(newIIDsSize, sizeof(ULONG_PTR));
	DWORD newSecWriteSize = thunkDataOffsetFromNewIIDsEntry + thunkDataSize;

	// ���ݼ�������Ĵ�С���������ڴ�������Section
	PIMAGE_SECTION_HEADER newlyAddedSecHeader = image.AddNewSectionToMemory(newSecName, newSecWriteSize);
	printf("[*] ����Section�ɹ���Section VA = 0x%X V.Size=0x%X R.Size = 0x%X\n",
		newlyAddedSecHeader->VirtualAddress,
		newlyAddedSecHeader->Misc.VirtualSize,
		newlyAddedSecHeader->SizeOfRawData);

	// ��ʼ����Ҫ������Section��IIDs��ע���DLL��Thunk����
	PBYTE secWriteBuffer = (PBYTE)calloc(1, newSecWriteSize);
	// ��ԭ�������뻺����
	SIZE_T dwIoCnt;
	ReadProcessMemory(
		pi.hProcess,
		imageBase + image.m_pImpDataDir->VirtualAddress,
		secWriteBuffer,
		oldIIDsSize,
		&dwIoCnt);
	printf("[*] ��ȡԭ������������ɹ���ReadSize = 0x%X\n", dwIoCnt);

	printf("[*] ��ʼ����Thunk����\n");
	// �����������λ��
	PULONG_PTR pOriginFirstThunk = (PULONG_PTR)(secWriteBuffer + thunkDataOffsetFromNewIIDsEntry);
	PULONG_PTR pFirstThunk = pOriginFirstThunk + 2;
	PIMAGE_IMPORT_BY_NAME pImpName = (PIMAGE_IMPORT_BY_NAME)(pFirstThunk + 2);
	PCHAR pDllName = (PCHAR)((PBYTE)pImpName + sizeof(WORD) + strlen(dllExpFunName) + 1);
	// ���INT���IAT��
	pOriginFirstThunk[0] = newlyAddedSecHeader->VirtualAddress + MEM_OFFSET(pImpName, secWriteBuffer);
	pFirstThunk[0] = pOriginFirstThunk[0];
	// �����IID��
	PIMAGE_IMPORT_DESCRIPTOR newIID = (PIMAGE_IMPORT_DESCRIPTOR)secWriteBuffer + oldIIDsCnt - 1;
	newIID->OriginalFirstThunk = newlyAddedSecHeader->VirtualAddress + MEM_OFFSET(pOriginFirstThunk, secWriteBuffer);
	newIID->FirstThunk = newlyAddedSecHeader->VirtualAddress + MEM_OFFSET(pFirstThunk, secWriteBuffer);
	newIID->Name = newlyAddedSecHeader->VirtualAddress + MEM_OFFSET(pDllName, secWriteBuffer);
	// ���IMAGE_IMPORT_BY_NAME�ṹ �� dll��
	strncpy(pDllName, dllname, strlen(dllname));
	pImpName->Hint = 0;
	strncpy(pImpName->Name, dllExpFunName, strlen(dllExpFunName));


	// ����image�����pe�ṹ��Ϣ������֮��д�ؽ����ڴ�
	image.m_pImpDataDir->Size = newIIDsSize;
	image.m_pImpDataDir->VirtualAddress = newlyAddedSecHeader->VirtualAddress;

	image.m_pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
	image.m_pOptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;
	printf("[*] PEͷ������ϣ�׼��д�����\n");

	DWORD oldProtect = 0;
	result = VirtualProtectEx(pi.hProcess, imageBase, image.m_pOptHeader->SizeOfHeaders, PAGE_READWRITE, &oldProtect);
	if (!result)
	{
		printf("[-] �޷��޸�Ŀ������ڴ� 0x%p Size = 0x%X���ڴ����� [%d]\n", imageBase, image.m_pOptHeader->SizeOfHeaders, GetLastError());
		return FALSE;
	}
	result = WriteProcessMemory(pi.hProcess, imageBase, image.m_HeaderData, image.m_pOptHeader->SizeOfHeaders, &dwIoCnt);
	if (!result)
	{
		printf("[-] ��Ŀ������ڴ�д���޸�ͷ������ʧ��\n");
		return FALSE;
	}
	VirtualProtectEx(pi.hProcess, imageBase, image.m_pOptHeader->SizeOfHeaders, oldProtect, &oldProtect);
	printf("[*] ��д���޸ĺ��PEͷ\n");

	result = WriteProcessMemory(pi.hProcess, imageBase + newlyAddedSecHeader->VirtualAddress, secWriteBuffer, newSecWriteSize, &dwIoCnt);
	if (!result) {
		printf("[-] ��Ŀ�����д���������ʧ�ܣ�%d\n", GetLastError());
		return FALSE;
	}
	printf("[*] ��д���������\n");




	ResumeThread(pi.hThread);
	return TRUE;
}

ULONG_PTR FindImageBase(HANDLE hProc, LPSTR lpCommandLine)
{
	ULONG_PTR imgAddressFound = 0;
	BOOL bFoundMemImage = FALSE;
	SYSTEM_INFO sysinfo = { 0 };
	GetSystemInfo(&sysinfo);

	char imageFilePath[MAX_PATH] = { 0 };
	char* fileNameToCheck = strrchr(lpCommandLine, '\\');

	PBYTE pAddress = (PBYTE)sysinfo.lpMinimumApplicationAddress;
	while (pAddress < (PBYTE)sysinfo.lpMaximumApplicationAddress)
	{
		MEMORY_BASIC_INFORMATION mbi;
		ZeroMemory(&mbi, sizeof(MEMORY_BASIC_INFORMATION));
		SIZE_T dwSize = VirtualQueryEx(hProc, pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION)); // ��ȡ��ͬ�����ڴ�ҳ����Ϣ
		if (dwSize == 0)
		{
			pAddress += sysinfo.dwPageSize;
			continue;
		}

		switch (mbi.State)
		{
		case MEM_FREE:
		case MEM_RESERVE:
			pAddress = (PBYTE)mbi.BaseAddress + mbi.RegionSize;
			break;
		case MEM_COMMIT:
			if (mbi.Type == MEM_IMAGE)
			{
				if (GetMappedFileNameA(hProc, pAddress, imageFilePath, MAX_PATH) != 0)
				{
					if (_stricmp(strrchr(imageFilePath, '\\'), fileNameToCheck) == 0)
					{
						bFoundMemImage = TRUE;
						imgAddressFound = (ULONG_PTR)pAddress;
						break;
					}
				}
			}
			pAddress = (PBYTE)mbi.BaseAddress + mbi.RegionSize;
			break;
		default:
			break;
		}

		if (bFoundMemImage)
		{
			break;
		}
	}
	return imgAddressFound;
}