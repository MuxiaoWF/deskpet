package com.muxiao.deskpet;

/**
 * Progress callback for model import operations (folder import / LPK unpack).
 * <p>
 * Stages progress through {@link #STAGE_SCAN} → {@link #STAGE_COPY} → {@link #STAGE_UNPACK}.
 * Indeterminate reporting uses negative done/total values.
 */
public interface ImportProgressListener {

    int STAGE_SCAN = 0;
    int STAGE_COPY = 1;
    int STAGE_UNPACK = 2;

    /**
     * @param stage one of STAGE_SCAN / STAGE_COPY / STAGE_UNPACK
     * @param done  items/percent completed, or -1 when indeterminate
     * @param total total items (100 for percent), or -1 when indeterminate
     */
    void onProgress(int stage, int done, int total);
}
