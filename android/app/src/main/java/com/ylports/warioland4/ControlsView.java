package com.ylports.warioland4;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/** Touch input is collected for the future native game loop; no game runs yet. */
public final class ControlsView extends View {
    public interface Sink {
        void setButtons(int buttons);
    }
    private final Sink sink;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int heldButtons;
    private static final int A = 1, B = 2, SELECT = 4, START = 8;
    private static final int RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128, R = 256, L = 512;

    public ControlsView(Context context, Sink sink) {
        super(context);
        this.sink = sink;
        setWillNotDraw(false);
    }

    private void circle(Canvas c, float x, float y, float radius, int button) {
        paint.setColor((heldButtons & button) != 0 ? 0x99ffffff : 0x44ffffff);
        c.drawCircle(x, y, radius, paint);
    }

    @Override protected void onDraw(Canvas canvas) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w / 13f, h / 6f);
        paint.setColor(0x44ffffff);
        float x = w * .18f, y = h * .72f;
        canvas.drawRoundRect(x - unit * 1.30f, y - unit * .36f, x + unit * 1.30f,
                y + unit * .36f, unit * .1f, unit * .1f, paint);
        canvas.drawRoundRect(x - unit * .36f, y - unit * 1.30f, x + unit * .36f,
                y + unit * 1.30f, unit * .1f, unit * .1f, paint);
        circle(canvas, w * .82f, h * .68f, unit * .72f, A);
        circle(canvas, w * .73f, h * .82f, unit * .68f, B);
        circle(canvas, w * .11f, h * .18f, unit * .6f, L);
        circle(canvas, w * .89f, h * .18f, unit * .6f, R);
        circle(canvas, w * .45f, h * .90f, unit * .30f, SELECT);
        circle(canvas, w * .56f, h * .90f, unit * .30f, START);
    }

    private int getButtons(float x, float y) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w / 13f, h / 6f);
        int keys = 0;
        float dx = x - w * .18f, dy = y - h * .72f;
        if (Math.abs(dx) < unit * 1.5f && Math.abs(dy) < unit * 1.5f) {
            if (dx < -unit * .35f) keys |= LEFT;
            if (dx > unit * .35f) keys |= RIGHT;
            if (dy < -unit * .35f) keys |= UP;
            if (dy > unit * .35f) keys |= DOWN;
        }
        if (dist(x, y, w * .82f, h * .68f) < unit * .9f) keys |= A;
        if (dist(x, y, w * .73f, h * .82f) < unit * .85f) keys |= B;
        if (dist(x, y, w * .11f, h * .18f) < unit * .75f) keys |= L;
        if (dist(x, y, w * .89f, h * .18f) < unit * .75f) keys |= R;
        if (dist(x, y, w * .45f, h * .90f) < unit * .40f) keys |= SELECT;
        if (dist(x, y, w * .56f, h * .90f) < unit * .40f) keys |= START;
        return keys;
    }

    private static float dist(float x, float y, float a, float b) {
        return (float) Math.hypot(x - a, y - b);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int buttons = 0;
        if (action != MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                if (action == MotionEvent.ACTION_UP
                        || (action == MotionEvent.ACTION_POINTER_UP
                        && i == event.getActionIndex())) continue;
                buttons |= getButtons(event.getX(i), event.getY(i));
            }
        }
        if (buttons != heldButtons) {
            heldButtons = buttons;
            sink.setButtons(buttons);
            invalidate();
        }
        return true;
    }
}
