package hev.mcplayer;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;

public class FullScreenSurfaceView extends SurfaceView
        implements View.OnSystemUiVisibilityChangeListener {

    private final Runnable _fullScreenTask;

    public FullScreenSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);

        setOnSystemUiVisibilityChangeListener(this);

        _fullScreenTask = new Runnable() {

            @Override
            public void run() {
                Log.d("fullscreen", "set full screen");
                setSystemUiVisibility(SYSTEM_UI_FLAG_FULLSCREEN
                        | SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | SYSTEM_UI_FLAG_IMMERSIVE);

            }

        };
    }

    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        Log.d("fullscreen", "onSystemUiVisibilityChange");
        if ((visibility & SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
            boolean s = getHandler().postDelayed(_fullScreenTask, 3000);
            Log.d("fullscreen", "set handler:" + s);
        }

    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        Log.d("Resolution", "onWindowVisibilityChanged(web view)");
        setSystemUiVisibility(SYSTEM_UI_FLAG_FULLSCREEN |
                SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                SYSTEM_UI_FLAG_IMMERSIVE);

    }
}
