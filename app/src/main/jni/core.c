#include <jni.h>
#include <string.h>
#include <pthread.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

/*
 * These macros provide a way to store the native pointer to GstNativePrivate,
 * which might be 32 or 64 bits, into a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
# define GET_PRIVATE_DATA(env, thiz, fieldID) (GstNativePrivate *)(*env)->GetLongField (env, thiz, fieldID)
# define SET_PRIVATE_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else
# define GET_PRIVATE_DATA(env, thiz, fieldID) (GstNativePrivate *)(jint)(*env)->GetLongField (env, thiz, fieldID)
# define SET_PRIVATE_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

typedef struct _GstNativePrivate GstNativePrivate;

struct _GstNativePrivate
{
    jobject app;
    gchar *pipeline_desc;

    GMainLoop *main_loop;
    GstElement *pipeline;

    jobject surface;
};

/* Java bindings */
static jboolean gst_native_class_init (JNIEnv *env, jclass klass);
static jboolean gst_native_init (JNIEnv *env, jobject thiz, jstring pipeline_desc);
static void gst_native_finalize (JNIEnv *env, jobject thiz);
static jboolean gst_native_surface_init (JNIEnv *env, jobject thiz, jobject surface);
static void gst_native_surface_finalize (JNIEnv *env, jobject thiz);

/* Private methods */
static void * gst_app_thread_handler (void *data);

static JavaVM *java_vm;
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static jfieldID private_field_id;

static JNINativeMethod native_methods[] =
{
    { "GstNativeClassInit", "()Z", (void *) gst_native_class_init },
    { "GstNativeInit", "(Ljava/lang/String;)Z", (void *) gst_native_init },
    { "GstNativeFinalize", "()V", (void *) gst_native_finalize },
    { "GstNativeSurfaceInit", "(Ljava/lang/Object;)Z", (void *) gst_native_surface_init },
    { "GstNativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize },
};

static JNIEnv *
attach_current_thread (void)
{
    JNIEnv *env;
    JavaVMAttachArgs args;

    GST_DEBUG ("Attaching thread %p", g_thread_self ());
    args.version = JNI_VERSION_1_4;
    args.name = NULL;
    args.group = NULL;

    if (0 > (*java_vm)->AttachCurrentThread (java_vm, &env, &args)) {
        GST_ERROR ("Failed to attach current thread");
        return NULL;
    }

    return env;
}

static void
detach_current_thread (void *env)
{
    GST_DEBUG ("Detaching thread %p", g_thread_self ());
    (*java_vm)->DetachCurrentThread (java_vm);
}

static JNIEnv *
get_jni_env (void)
{
    JNIEnv *env;

    if ((env = pthread_getspecific (current_jni_env)) == NULL) {
        env = attach_current_thread ();
        pthread_setspecific (current_jni_env, env);
    }

    return env;
}

jint
JNI_OnLoad (JavaVM *vm, void *reserved)
{
    JNIEnv *env = NULL;

    java_vm = vm;
    if (JNI_OK != (*vm)->GetEnv (vm, (void**) &env, JNI_VERSION_1_4)) {
        GST_ERROR ("Could not retrieve JNIEnv");
        return 0;
    }
    jclass klass = (*env)->FindClass (env, "hev/mcplayer/MCPlayer");
    (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS (native_methods));
    (*env)->DeleteLocalRef (env, klass);

    pthread_key_create (&current_jni_env, detach_current_thread);

    return JNI_VERSION_1_4;
}

static jboolean
gst_native_class_init (JNIEnv *env, jclass klass)
{
    private_field_id = (*env)->GetFieldID (env, klass, "native_private", "J");
    GST_DEBUG ("The FieldID for the native_private field is %p", private_field_id);

    if (!private_field_id) {
        GST_ERROR ("The calling class does not implement all necessary interface methods");
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

static jboolean
gst_native_init (JNIEnv *env, jobject thiz, jstring pipeline_desc)
{
    const char *desc = NULL;

    /* Create GstNativePrivate */
    GstNativePrivate *priv = g_new0 (GstNativePrivate, 1);
    SET_PRIVATE_DATA (env, thiz, private_field_id, priv);
    GST_DEBUG ("Created GstNativePrivate at %p", priv);

    /* Create GlobalRef for app */
    priv->app = (*env)->NewGlobalRef (env, thiz);
    GST_DEBUG ("Created GlobalRef for app object at %p", priv->app);

    /* Pipeline desc string */
    desc = (*env)->GetStringUTFChars (env, pipeline_desc, NULL);
    priv->pipeline_desc = g_strdup (desc);
    GST_DEBUG ("Duped pipeline desc string %s", desc);
    (*env)->ReleaseStringUTFChars (env, pipeline_desc, desc);

    /* Create GStreamer app main thread */
    pthread_create (&gst_app_thread, NULL, &gst_app_thread_handler, priv);

    GST_DEBUG ("GstNativeInit is done");

    return JNI_TRUE;
}

static void
gst_native_finalize (JNIEnv *env, jobject thiz)
{
    GstNativePrivate *priv = GET_PRIVATE_DATA (env, thiz, private_field_id);
    if (!priv)
      return;

    /* Quitting main loop */
    GST_DEBUG ("Quitting main loop ...");
    g_main_loop_quit (priv->main_loop);

    /* Waiting for GStreamer main thread */
    GST_DEBUG ("Waiting for thread to finish ...");
    pthread_join (gst_app_thread, NULL);

    /* Delete GblobalRef for app */
    GST_DEBUG ("Deleting GlobalRef for app object at %p ...", priv->app);
    (*env)->DeleteGlobalRef (env, priv->app);

    /* Free pipeline desc string */
    GST_DEBUG ("Freeing pipeline desc ...");
    g_free (priv->pipeline_desc);

    /* Free GstNativePrivate */
    GST_DEBUG ("Freeing GstNativePrivate at %p ...", priv);
    g_free (priv);
    SET_PRIVATE_DATA (env, thiz, private_field_id, NULL);

    GST_DEBUG ("GstNativeFinalize is done");
}

static jboolean
gst_native_surface_init (JNIEnv *env, jobject thiz, jobject surface)
{
    GstNativePrivate *priv = GET_PRIVATE_DATA (env, thiz, private_field_id);
    if (!priv)
      return JNI_FALSE;

    GST_DEBUG ("Received surface %p", surface);

    if (priv->surface)
        (*env)->DeleteGlobalRef (env, priv->surface);
    priv->surface = (*env)->NewGlobalRef (env, surface);

    if (priv->pipeline) {
        GST_DEBUG ("Pipeline already created, "
                    "notifying the it about the surface.");

        /* Set GStreamer pipeline to playing */
        gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
    }

    return JNI_TRUE;
}

static void
gst_native_surface_finalize (JNIEnv *env, jobject thiz)
{
    GstNativePrivate *priv = GET_PRIVATE_DATA (env, thiz, private_field_id);
    if (!priv) {
        GST_WARNING ("Received surface finalize "
                    "but there is no GstNativePrivate. Ignoring.");
        return;
    }

    if (priv->pipeline)
        gst_element_set_state (priv->pipeline, GST_STATE_NULL);

    if (priv->surface) {
        (*env)->DeleteGlobalRef (env, priv->surface);
        priv->surface = NULL;
    }
}

static GstBusSyncReply
prepare_window (GstBus * bus, GstMessage * message, gpointer data)
{
    GstNativePrivate *priv = data;

    if (!gst_is_video_overlay_prepare_window_handle_message (message))
        return GST_BUS_PASS;

    if (priv->surface) {
        JNIEnv *env = get_jni_env ();
        gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message)),
                    (guintptr) priv->surface);
        (*env)->DeleteGlobalRef (env, priv->surface);
        priv->surface = NULL;
    }

    gst_message_unref (message);

    return GST_BUS_DROP;
}

static void *
gst_app_thread_handler (void *data)
{
    GstNativePrivate *priv = data;
    GError *err = NULL;
    GstBus *bus;

    /* Create GStreamer pipeline */
    GST_DEBUG ("Creating GStreamer pipeline ...");
    priv->pipeline = gst_parse_launch (priv->pipeline_desc, &err);
    if (!priv->pipeline) {
        GST_ERROR ("Create pipeline from pipeline desc failed; %s", err->message);
        g_clear_error (&err);
        return NULL;
    }

    bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
    gst_bus_set_sync_handler (bus, (GstBusSyncHandler) prepare_window, priv, NULL);
    gst_object_unref (bus);

    /* Surface already created, playing now */
    if (priv->surface)
        gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);

    /* Create GMainLoop and set it to run */
    GST_DEBUG ("Entering main loop ...");
    priv->main_loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (priv->main_loop);
    GST_DEBUG ("Exited main loop");
    g_main_loop_unref (priv->main_loop);

    /* Free resources */
    gst_element_set_state (priv->pipeline, GST_STATE_NULL);
    gst_object_unref (priv->pipeline);

    return NULL;
}
