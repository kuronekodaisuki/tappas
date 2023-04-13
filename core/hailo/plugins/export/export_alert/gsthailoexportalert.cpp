/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "gsthailoexportalert.hpp"
#include "gst_hailo_meta.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <gst/video/video.h>
#include <gst/gst.h>

#define RAPIDJSON_HAS_STDSTRING 1
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"

GST_DEBUG_CATEGORY_STATIC(gst_hailoexportalert_debug_category);
#define GST_CAT_DEFAULT gst_hailoexportalert_debug_category

/* prototypes */

static void gst_hailoexportalert_set_property(GObject *object,
                                          guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_hailoexportalert_get_property(GObject *object,
                                          guint property_id, GValue *value, GParamSpec *pspec);
static void gst_hailoexportalert_dispose(GObject *object);
static void gst_hailoexportalert_finalize(GObject *object);

static gboolean gst_hailoexportalert_start(GstBaseTransform *trans);
static gboolean gst_hailoexportalert_stop(GstBaseTransform *trans);
static GstFlowReturn gst_hailoexportalert_transform_ip(GstBaseTransform *trans,
                                                   GstBuffer *buffer);

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstHailoExportALERT, gst_hailoexportalert, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_hailoexportalert_debug_category, "hailoexportalert", 0,
                                                "debug category for hailoexportalert element"));

enum
{
    PROP_0,
    PROP_ADDRESS,
};

static void
gst_hailoexportalert_class_init(GstHailoExportALERTClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class =
        GST_BASE_TRANSFORM_CLASS(klass);

    const char *description = "Exports HailoObjects in JSON format to a ALERT socket."
                              "\n\t\t\t   "
                              "Encodes classes contained by HailoROI objects to JSON.";
    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                            gst_caps_new_any()));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
                                       gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                            gst_caps_new_any()));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
                                          "hailoexportalert - export element",
                                          "Hailo/Tools",
                                          description,
                                          "hailo.ai <contact@hailo.ai>");

    gobject_class->set_property = gst_hailoexportalert_set_property;
    gobject_class->get_property = gst_hailoexportalert_get_property;
    g_object_class_install_property(gobject_class, PROP_ADDRESS,
                                    g_param_spec_string("address", "Endpoint address.",
                                                        "Address to bind the socket to.", "tcp://*:5555",
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    gobject_class->dispose = gst_hailoexportalert_dispose;
    gobject_class->finalize = gst_hailoexportalert_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_hailoexportalert_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_hailoexportalert_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_hailoexportalert_transform_ip);
}

static void
gst_hailoexportalert_init(GstHailoExportALERT *hailoexportalert)
{
    hailoexportalert->address = g_strdup("tcp://*:5555");
    hailoexportalert->buffer_offset = 0;
}

void gst_hailoexportalert_set_property(GObject *object, guint property_id,
                                   const GValue *value, GParamSpec *pspec)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(object);

    GST_DEBUG_OBJECT(hailoexportalert, "set_property");

    switch (property_id)
    {
    case PROP_ADDRESS:
        hailoexportalert->address = g_strdup(g_value_get_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoexportalert_get_property(GObject *object, guint property_id,
                                   GValue *value, GParamSpec *pspec)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(object);

    GST_DEBUG_OBJECT(hailoexportalert, "get_property");

    switch (property_id)
    {
    case PROP_ADDRESS:
        g_value_set_string(value, hailoexportalert->address);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_hailoexportalert_dispose(GObject *object)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(object);
    GST_DEBUG_OBJECT(hailoexportalert, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_hailoexportalert_parent_class)->dispose(object);
}

void gst_hailoexportalert_finalize(GObject *object)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(object);
    GST_DEBUG_OBJECT(hailoexportalert, "finalize");

    /* clean up object here */

    G_OBJECT_CLASS(gst_hailoexportalert_parent_class)->finalize(object);
}

static gboolean
gst_hailoexportalert_start(GstBaseTransform *trans)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(trans);
    GST_DEBUG_OBJECT(hailoexportalert, "start");

    // Prepare the context and socket
    hailoexportalert->context = new alert::context_t(1);
    hailoexportalert->socket = new alert::socket_t(*(hailoexportalert->context), ALERT_PUB);

    // Bind the socket to the requested address
    hailoexportalert->socket->bind(hailoexportalert->address);

    return TRUE;
}

static gboolean
gst_hailoexportalert_stop(GstBaseTransform *trans)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(trans);
    GST_DEBUG_OBJECT(hailoexportalert, "stop");

    // Unbind the socket and close the context
    hailoexportalert->socket->close();
    hailoexportalert->context->close();

    return TRUE;
}

static GstFlowReturn
gst_hailoexportalert_transform_ip(GstBaseTransform *trans,
                                 GstBuffer *buffer)
{
    GstHailoExportALERT *hailoexportalert = GST_HAILO_EXPORT_ALERT(trans);

    // Get the roi from the current buffer and encode it to a JSON entry
    HailoROIPtr hailo_roi = get_hailo_main_roi(buffer, true);
    rapidjson::Document encoded_roi = encode_json::encode_hailo_roi(hailo_roi);

    // Add a timestamp
    auto timenow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    encoded_roi.AddMember("timestamp (ms)", rapidjson::Value(timenow), encoded_roi.GetAllocator());
    encoded_roi.AddMember("buffer_offset", rapidjson::Value(hailoexportalert->buffer_offset), encoded_roi.GetAllocator());

    // Get the buffer of the json
    rapidjson::StringBuffer json_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);
    encoded_roi.Accept(writer);

    // Send the message
    alert::message_t json_message(json_buffer.GetSize());
    // Copy is required since alert::message_t would only wrap the data, so if the buffer is freed/overwritten
    // while the message is sending you will get garbage data or a segfault.
    std::memcpy(json_message.data(), json_buffer.GetString(), json_buffer.GetSize()); 
#if (CPPALERT_VERSION_MAJOR >= 4 && CPPALERT_VERSION_MINOR >= 6 && CPPALERT_VERSION_PATCH >= 0)
    alert::send_result_t result = hailoexportalert->socket->send(json_message, alert::send_flags(ALERT_DONTWAIT));
#else
    alert::detail::send_result_t result = hailoexportalert->socket->send(json_message, alert::send_flags(ALERT_DONTWAIT));
#endif
    if (result != json_message.size())
        GST_WARNING("hailoexportalert failed to send buffer!");

    hailoexportalert->buffer_offset++;
    GST_DEBUG_OBJECT(hailoexportalert, "transform_ip");
    return GST_FLOW_OK;
}
