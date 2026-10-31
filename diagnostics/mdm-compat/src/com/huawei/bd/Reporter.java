package com.huawei.bd;

import android.content.Context;
import org.json.JSONObject;

/** Compatibility for optional Huawei event reporting in system apps. */
public final class Reporter {
    private Reporter() {}

    /** No reporting service is provided by this ROM. */
    public static boolean c(Context context, int event) {
        return false;
    }

    /** No reporting service is provided by this ROM. */
    public static boolean c(Context context, int event, int extra) {
        return false;
    }

    /** No reporting service is provided by this ROM. */
    public static boolean e(Context context, int event, String details) {
        return false;
    }

    public static boolean beta(Context context, int event, String details) {
        return false;
    }

    public static boolean e(Context context, int event, String details, int extra) {
        return false;
    }

    public static boolean j(Context context, int event, JSONObject details) {
        return false;
    }

    public static boolean j(Context context, int event, JSONObject details, int extra) {
        return false;
    }
}
