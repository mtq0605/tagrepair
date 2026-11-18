package com.omni.tagrepair.core;

/** 一个文件属于哪一类(纯函数;与 Windows 版 src/classify.h 相同) */
public final class Classify {
    private Classify() {}

    public enum State { ERROR, NOT_LOSSLESS, NO_MARKER, COMPLETE, REPAIRABLE, RATE_UNKNOWN, UNSUPPORTED, DAMAGED, CONVERTIBLE }

    public static final class Facts {
        public boolean opened, losslessPcm, marker, nativeFlac, flacDamaged, rateAvailable, wavPcm;
        public int bad;
    }

    public static State classify(Facts f) {
        if (!f.opened) return State.ERROR;
        if (!f.losslessPcm) return State.NOT_LOSSLESS;
        if (!f.marker) return State.NO_MARKER;
        if (!f.nativeFlac) return f.wavPcm && f.rateAvailable ? State.CONVERTIBLE : State.UNSUPPORTED;
        if (f.flacDamaged) return State.DAMAGED;
        if (TagRule.tagsComplete(f.bad)) return State.COMPLETE;
        if ((f.bad & TagRule.BAD_ORIG_RATE) != 0 && !f.rateAvailable) return State.RATE_UNKNOWN;
        return State.REPAIRABLE;
    }

    public static String stateCode(State s) {
        switch (s) {
        case ERROR: return "error";
        case NOT_LOSSLESS: return "not_lossless";
        case NO_MARKER: return "no_marker";
        case COMPLETE: return "complete";
        case REPAIRABLE: return "repairable";
        case RATE_UNKNOWN: return "rate_unknown";
        case UNSUPPORTED: return "unsupported_container";
        case DAMAGED: return "damaged";
        case CONVERTIBLE: return "convertible";
        default: return "error";
        }
    }
}
