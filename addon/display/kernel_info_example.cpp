// Example code showing how to determine kernel information at runtime
#include <circle/machineinfo.h>
#include <circle/logger.h>
#include <circle/string.h>

// This demonstrates several methods to determine which kernel you're running

void DisplayKernelInfo(CLogger* pLogger) {
    // Method 1: Use compile-time RASPPI macro to determine which kernel was built
    #if RASPPI == 1
        const char* kernelName = "kernel.img";
        const char* architecture = "ARMv6 (Pi 1/Zero)";
    #elif RASPPI == 2
        const char* kernelName = "kernel7.img"; 
        const char* architecture = "ARMv7 (Pi 2)";
    #elif RASPPI == 3
        const char* kernelName = "kernel8-32.img";
        const char* architecture = "ARMv8-32 (Pi 3)";
    #elif RASPPI == 4
        const char* kernelName = "kernel7l.img";
        const char* architecture = "ARMv7 (Pi 4 32-bit)";
    #elif RASPPI == 5
        const char* kernelName = "kernel_2712.img";
        const char* architecture = "ARMv8-64 (Pi 5)";
    #else
        const char* kernelName = "unknown";
        const char* architecture = "Unknown";
    #endif
    
    pLogger->Write("kernel", LogNotice, "Compiled as: %s (%s)", kernelName, architecture);
    
    // Method 2: Use runtime machine detection
    CMachineInfo* pMachineInfo = CMachineInfo::Get();
    TMachineModel model = pMachineInfo->GetMachineModel();
    TSoCType socType = pMachineInfo->GetSoCType();
    
    const char* runtimeKernel = "unknown";
    const char* modelName = pMachineInfo->GetMachineName();
    const char* socName = pMachineInfo->GetSoCName();
    
    // Determine likely kernel based on runtime detection
    switch (socType) {
        case SoCTypeBCM2835:
            runtimeKernel = "kernel.img (BCM2835)";
            break;
        case SoCTypeBCM2836:
            runtimeKernel = "kernel7.img (BCM2836)"; 
            break;
        case SoCTypeBCM2837:
            if (model == MachineModel3B || model == MachineModel3BPlus || model == MachineModel3APlus) {
                runtimeKernel = "kernel8-32.img (BCM2837)";
            } else {
                runtimeKernel = "kernel7.img (BCM2837)";
            }
            break;
        case SoCTypeBCM2711:
            runtimeKernel = "kernel7l.img (BCM2711)";
            break;
        case SoCTypeBCM2712:
            runtimeKernel = "kernel_2712.img (BCM2712)";
            break;
        default:
            runtimeKernel = "unknown SoC";
            break;
    }
    
    pLogger->Write("kernel", LogNotice, "Runtime detection: %s on %s (%s)", 
                   runtimeKernel, modelName, socName);
    
    // Method 3: Additional hardware info
    unsigned ramSize = pMachineInfo->GetRAMSize();
    unsigned modelMajor = pMachineInfo->GetModelMajor();
    unsigned modelRevision = pMachineInfo->GetModelRevision();
    
    pLogger->Write("kernel", LogNotice, "Hardware: Pi %u Rev %u, %u MB RAM", 
                   modelMajor, modelRevision, ramSize);
}

// Method 4: Create a function that returns the expected kernel filename
const char* GetKernelFilename() {
    CMachineInfo* pMachineInfo = CMachineInfo::Get();
    TSoCType socType = pMachineInfo->GetSoCType();
    TMachineModel model = pMachineInfo->GetMachineModel();
    
    switch (socType) {
        case SoCTypeBCM2835:
            return "kernel.img";
            
        case SoCTypeBCM2836:
            return "kernel7.img";
            
        case SoCTypeBCM2837:
            // Pi 3 models can use either kernel7.img or kernel8-32.img
            // depending on how it was built
            #if RASPPI == 3
                return "kernel8-32.img";
            #else
                return "kernel7.img";
            #endif
            
        case SoCTypeBCM2711:
            return "kernel7l.img";
            
        case SoCTypeBCM2712:
            return "kernel_2712.img";
            
        default:
            return "unknown";
    }
}

// Method 5: Get a descriptive string of the current platform
CString GetPlatformDescription() {
    CString result;
    CMachineInfo* pMachineInfo = CMachineInfo::Get();
    
    const char* machineName = pMachineInfo->GetMachineName();
    const char* socName = pMachineInfo->GetSoCName();
    const char* kernelFile = GetKernelFilename();
    
    #if RASPPI == 1
        result.Format("%s (%s) - %s [ARMv6]", machineName, socName, kernelFile);
    #elif RASPPI == 2
        result.Format("%s (%s) - %s [ARMv7]", machineName, socName, kernelFile);
    #elif RASPPI == 3
        result.Format("%s (%s) - %s [ARMv8-32]", machineName, socName, kernelFile);
    #elif RASPPI == 4
        result.Format("%s (%s) - %s [ARMv7L]", machineName, socName, kernelFile);
    #elif RASPPI == 5
        result.Format("%s (%s) - %s [ARMv8-64]", machineName, socName, kernelFile);
    #else
        result.Format("%s (%s) - %s [Unknown]", machineName, socName, kernelFile);
    #endif
    
    return result;
}
