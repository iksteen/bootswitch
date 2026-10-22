/* QEMU-only payload: report systemd-boot's chosen entry and stop the VM. */
#include <efi.h>
#include <efilib.h>

static EFI_GUID loader_guid =
    {0x4a67b082, 0x0a4c, 0x41cf, {0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f}};

static void debug_char(CHAR8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"((UINT16)0xe9));
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table) {
    InitializeLib(image, table);
#ifdef SEED_ONESHOT
    CHAR16 *entry = L"windows.conf";
    EFI_STATUS status = uefi_call_wrapper(RT->SetVariable, 5, L"LoaderEntryOneShot",
        &loader_guid, EFI_VARIABLE_NON_VOLATILE |
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        StrSize(entry), entry);
    if (EFI_ERROR(status)) return status;
    EFI_LOADED_IMAGE *self;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, image,
                               &LoadedImageProtocol, (VOID **)&self);
    if (EFI_ERROR(status)) return status;
    EFI_DEVICE_PATH *path = FileDevicePath(self->DeviceHandle,
                                           L"\\EFI\\BootSwitch\\bootswitchx64.efi");
    EFI_HANDLE child;
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &child);
    if (EFI_ERROR(status)) return status;
    return uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
#else
    CHAR16 entry[256];
    UINTN size = sizeof(entry);
    EFI_STATUS status = uefi_call_wrapper(RT->GetVariable, 5, L"LoaderEntrySelected",
                                         &loader_guid, NULL, &size, entry);
    CHAR8 *prefix = (CHAR8 *)"selected=";
    while (*prefix) debug_char(*prefix++);
    if (!EFI_ERROR(status)) {
        UINTN i;
        for (i = 0; i < 255 && entry[i]; ++i) debug_char((CHAR8)entry[i]);
    }
    debug_char('\n');
    size = 0;
    status = uefi_call_wrapper(RT->GetVariable, 5, L"LoaderEntryOneShot",
                               &loader_guid, NULL, &size, NULL);
    CHAR8 *result = (CHAR8 *)(status == EFI_NOT_FOUND ? "oneshot=consumed\n" :
                                                                    "oneshot=present\n");
    while (*result) debug_char(*result++);
    __asm__ volatile ("outl %0, %1" : : "a"((UINT32)0x10), "Nd"((UINT16)0xf4));
    return EFI_SUCCESS;
#endif
}
