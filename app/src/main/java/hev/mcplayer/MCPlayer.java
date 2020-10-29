package hev.mcplayer;

import android.app.Activity;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

import org.freedesktop.gstreamer.GStreamer;

public class MCPlayer extends Activity implements SurfaceHolder.Callback {
    private static native boolean GstNativeClassInit();
    private native boolean GstNativeInit(String pipeline_desc);
    private native void GstNativeFinalize();
    private native boolean GstNativeSurfaceInit(Object surface);
    private native void GstNativeSurfaceFinalize();
    private long native_private;

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("mcplayer");
        GstNativeClassInit();
    }

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        try {
            GStreamer.init(this);
        } catch (Exception e) {
            finish();
            return;
        }

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);

        setContentView(R.layout.main);

        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);

        GstNativeInit("playbin uri=file:///sdcard/mcplayer.mp4 video-sink=amcvideosink");
    }

    @Override
    protected void onDestroy() {
        GstNativeFinalize();
        super.onDestroy();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        GstNativeSurfaceInit (holder.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        GstNativeSurfaceFinalize ();
    }
}
