/* x86-64 UEFI bridge for systemd-boot. Uses GNU-EFI; no OS or network needed. */
#include <efi.h>
#include <efilib.h>

static EFI_GUID loader_guid =
    {0x4a67b082, 0x0a4c, 0x41cf, {0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f}};
static CHAR16 linux_id[256];
static CHAR16 windows_id[256];
static CHAR16 loader_path[256] = L"\\EFI\\systemd\\systemd-bootx64.efi";

static EFI_STATUS read_file(EFI_HANDLE device, CHAR16 *path,
                            CHAR8 *data, UINTN capacity, UINTN *length) {
    EFI_FILE_IO_INTERFACE *fs = NULL;
    EFI_FILE_HANDLE root = NULL, file = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, device,
                                          &FileSystemProtocol, (VOID **)&fs);
    if (EFI_ERROR(status)) return status;
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) return status;
    status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                               EFI_FILE_MODE_READ, 0);
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status)) return status;
    *length = capacity - 1;
    status = uefi_call_wrapper(file->Read, 3, file, length, data);
    if (!EFI_ERROR(status)) {
        // Reject truncation instead of accepting a prefix of a larger file.
        CHAR8 extra;
        UINTN count = 1;
        EFI_STATUS tail = uefi_call_wrapper(file->Read, 3, file, &count, &extra);
        if (EFI_ERROR(tail) || count != 0) status = EFI_BAD_BUFFER_SIZE;
    }
    uefi_call_wrapper(file->Close, 1, file);
    data[*length] = 0;
    return status;
}

static BOOLEAN ascii_equal(const CHAR8 *a, const CHAR8 *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static BOOLEAN copy_ascii(CHAR16 *out, const CHAR8 *in) {
    UINTN i;
    for (i = 0; in[i]; ++i) {
        if (i >= 255 || (UINT8)in[i] < 0x20 || (UINT8)in[i] > 0x7e)
            return FALSE;
        out[i] = in[i];
    }
    out[i] = 0;
    return i > 0;
}

static BOOLEAN load_config(EFI_HANDLE device) {
    CHAR8 data[2048];
    UINTN size;
    EFI_STATUS status = read_file(device, L"\\EFI\\BootSwitch\\bootswitch.conf",
                                  data, sizeof(data), &size);
    if (EFI_ERROR(status)) return FALSE;
    CHAR8 *line = data;
    while (*line) {
        CHAR8 *next = line;
        while (*next && *next != '\n') ++next;
        if (*next) *next++ = 0;
        while (*line == ' ' || *line == '\t') ++line;
        CHAR8 *end = line;
        while (*end) ++end;
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            *--end = 0;
        if (*line && *line != '#') {
            CHAR8 *value = line;
            while (*value && *value != '=') ++value;
            if (!*value) return FALSE;
            *value++ = 0;
            if (ascii_equal(line, (CHAR8 *)"linux")) {
                if (!copy_ascii(linux_id, value)) return FALSE;
            } else if (ascii_equal(line, (CHAR8 *)"windows")) {
                if (!copy_ascii(windows_id, value)) return FALSE;
            } else if (ascii_equal(line, (CHAR8 *)"loader")) {
                if (!copy_ascii(loader_path, value)) return FALSE;
            } else return FALSE;
        }
        line = next;
    }
    // Restrict the next program to the known systemd-boot path to prevent an
    // accidental recursion through this application or a fallback boot path.
    return linux_id[0] && windows_id[0] &&
           StriCmp(loader_path, L"\\EFI\\systemd\\systemd-bootx64.efi") == 0;
}

static BOOLEAN is_switch(EFI_HANDLE device) {
    EFI_FILE_IO_INTERFACE *fs;
    EFI_FILE_HANDLE root;
    if (EFI_ERROR(uefi_call_wrapper(BS->HandleProtocol, 3, device,
                                    &FileSystemProtocol, (VOID **)&fs))) return FALSE;
    if (EFI_ERROR(uefi_call_wrapper(fs->OpenVolume, 2, fs, &root))) return FALSE;
    EFI_FILE_SYSTEM_INFO *info = LibFileSystemInfo(root);
    BOOLEAN matches = info && StrCmp(info->VolumeLabel, L"BOOTSWITCH") == 0;
    if (info) FreePool(info);
    uefi_call_wrapper(root->Close, 1, root);
    return matches;
}

// -1 = missing/invalid/ambiguous, 0 = Linux, 1 = Windows.
static INTN scan_mode(VOID) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol,
                                          &FileSystemProtocol, NULL, &count, &handles);
    if (EFI_ERROR(status)) return -1;
    INTN mode = -1;
    UINTN found = 0, i;
    for (i = 0; i < count; ++i) {
        if (!is_switch(handles[i])) continue;
        CHAR8 data[32];
        UINTN size;
        status = read_file(handles[i], L"\\MODE.TXT", data, sizeof(data), &size);
        if (EFI_ERROR(status)) continue;
        // Match the complete fixed-length wire format, not an arbitrary prefix.
        if (size != 8) continue;
        if (CompareMem(data, "linux  \n", 8) == 0) mode = 0;
        else if (CompareMem(data, "windows\n", 8) == 0) mode = 1;
        else continue;
        ++found;
    }
    FreePool(handles);
    return found == 1 ? mode : -1;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image, system_table);
    EFI_LOADED_IMAGE *self;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, image,
                                          &LoadedImageProtocol, (VOID **)&self);
    if (EFI_ERROR(status)) return status;
    BOOLEAN configured = load_config(self->DeviceHandle);
    if (!configured) {
        // Never use a partial/invalid loader configuration for the fallback.
        StrCpy(loader_path, L"\\EFI\\systemd\\systemd-bootx64.efi");
        Print(L"BootSwitch: invalid/missing config; using systemd-boot defaults.\r\n");
    }
    EFI_DEVICE_PATH *path = FileDevicePath(self->DeviceHandle, loader_path);
    if (!path) return EFI_OUT_OF_RESOURCES;
    EFI_HANDLE child = NULL;
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &child);
    FreePool(path);
    if (EFI_ERROR(status)) {
        Print(L"BootSwitch: could not load systemd-boot: %r\r\n", status);
        return status;
    }

    BOOLEAN wrote_selection = FALSE;
    UINTN existing_size = 0;
    status = uefi_call_wrapper(RT->GetVariable, 5, L"LoaderEntryOneShot",
                               &loader_guid, NULL, &existing_size, NULL);
    // Explicit OS requests (including update/recovery reboots) take precedence.
    if (configured && status == EFI_NOT_FOUND) {
        INTN mode = -1;
        UINTN attempt;
        for (attempt = 0; attempt < 20; ++attempt) {
            mode = scan_mode();
            if (mode >= 0) break;
            uefi_call_wrapper(BS->Stall, 1, 100000);
        }
        if (mode >= 0) {
            CHAR16 *id = mode == 1 ? windows_id : linux_id;
            // systemd-boot clears this with the standard non-volatile attributes.
            // A volatile variable is read but may not be deleted by firmware.
            status = uefi_call_wrapper(RT->SetVariable, 5, L"LoaderEntryOneShot",
                &loader_guid, EFI_VARIABLE_NON_VOLATILE |
                EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                StrSize(id), id);
            wrote_selection = !EFI_ERROR(status);
            if (EFI_ERROR(status)) Print(L"BootSwitch: selection failed: %r\r\n", status);
            else Print(L"BootSwitch: selected %s\r\n", id);
        } else Print(L"BootSwitch: no unique selector; using systemd-boot defaults.\r\n");
    }
    status = uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
    // A loader that returns must not leave our request behind for another loader.
    if (wrote_selection)
        uefi_call_wrapper(RT->SetVariable, 5, L"LoaderEntryOneShot", &loader_guid, 0, 0, NULL);
    uefi_call_wrapper(BS->UnloadImage, 1, child);
    return status;
}
