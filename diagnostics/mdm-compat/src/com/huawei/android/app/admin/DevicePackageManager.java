package com.huawei.android.app.admin;
import android.content.ComponentName;
import huawei.android.app.admin.HwDevicePolicyManagerEx;
import java.util.List;
import java.util.ArrayList;

/** Compatibility adapter for the ROM's missing legacy MDM facade. */
public final class DevicePackageManager {
    private final HwDevicePolicyManagerEx policy = new HwDevicePolicyManagerEx();
    public List<String> getDisallowedUninstallPackageList(ComponentName admin) {
        return policy.getDisallowedUninstallPackageList(admin);
    }
    public List<String> getDisabledDeactivateMdmPackageList(ComponentName admin) {
        return policy.getDisabledDeactivateMdmPackageList(admin);
    }
    public ArrayList<String> getAllowNotificationApps(ComponentName admin) {
        return policy.getStringArrayPolicy(admin, "notification-app-bt-white-list");
    }
}
