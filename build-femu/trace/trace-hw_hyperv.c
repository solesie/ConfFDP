/* This file is autogenerated by tracetool, do not edit. */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "trace-hw_hyperv.h"

uint16_t _TRACE_VMBUS_RECV_MESSAGE_DSTATE;
uint16_t _TRACE_VMBUS_SIGNAL_EVENT_DSTATE;
uint16_t _TRACE_VMBUS_CHANNEL_NOTIFY_GUEST_DSTATE;
uint16_t _TRACE_VMBUS_POST_MSG_DSTATE;
uint16_t _TRACE_VMBUS_MSG_CB_DSTATE;
uint16_t _TRACE_VMBUS_PROCESS_INCOMING_MESSAGE_DSTATE;
uint16_t _TRACE_VMBUS_INITIATE_CONTACT_DSTATE;
uint16_t _TRACE_VMBUS_SEND_OFFER_DSTATE;
uint16_t _TRACE_VMBUS_TERMINATE_OFFERS_DSTATE;
uint16_t _TRACE_VMBUS_GPADL_HEADER_DSTATE;
uint16_t _TRACE_VMBUS_GPADL_BODY_DSTATE;
uint16_t _TRACE_VMBUS_GPADL_CREATED_DSTATE;
uint16_t _TRACE_VMBUS_GPADL_TEARDOWN_DSTATE;
uint16_t _TRACE_VMBUS_GPADL_TORNDOWN_DSTATE;
uint16_t _TRACE_VMBUS_OPEN_CHANNEL_DSTATE;
uint16_t _TRACE_VMBUS_CHANNEL_OPEN_DSTATE;
uint16_t _TRACE_VMBUS_CLOSE_CHANNEL_DSTATE;
TraceEvent _TRACE_VMBUS_RECV_MESSAGE_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_recv_message",
    .sstate = TRACE_VMBUS_RECV_MESSAGE_ENABLED,
    .dstate = &_TRACE_VMBUS_RECV_MESSAGE_DSTATE 
};
TraceEvent _TRACE_VMBUS_SIGNAL_EVENT_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_signal_event",
    .sstate = TRACE_VMBUS_SIGNAL_EVENT_ENABLED,
    .dstate = &_TRACE_VMBUS_SIGNAL_EVENT_DSTATE 
};
TraceEvent _TRACE_VMBUS_CHANNEL_NOTIFY_GUEST_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_channel_notify_guest",
    .sstate = TRACE_VMBUS_CHANNEL_NOTIFY_GUEST_ENABLED,
    .dstate = &_TRACE_VMBUS_CHANNEL_NOTIFY_GUEST_DSTATE 
};
TraceEvent _TRACE_VMBUS_POST_MSG_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_post_msg",
    .sstate = TRACE_VMBUS_POST_MSG_ENABLED,
    .dstate = &_TRACE_VMBUS_POST_MSG_DSTATE 
};
TraceEvent _TRACE_VMBUS_MSG_CB_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_msg_cb",
    .sstate = TRACE_VMBUS_MSG_CB_ENABLED,
    .dstate = &_TRACE_VMBUS_MSG_CB_DSTATE 
};
TraceEvent _TRACE_VMBUS_PROCESS_INCOMING_MESSAGE_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_process_incoming_message",
    .sstate = TRACE_VMBUS_PROCESS_INCOMING_MESSAGE_ENABLED,
    .dstate = &_TRACE_VMBUS_PROCESS_INCOMING_MESSAGE_DSTATE 
};
TraceEvent _TRACE_VMBUS_INITIATE_CONTACT_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_initiate_contact",
    .sstate = TRACE_VMBUS_INITIATE_CONTACT_ENABLED,
    .dstate = &_TRACE_VMBUS_INITIATE_CONTACT_DSTATE 
};
TraceEvent _TRACE_VMBUS_SEND_OFFER_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_send_offer",
    .sstate = TRACE_VMBUS_SEND_OFFER_ENABLED,
    .dstate = &_TRACE_VMBUS_SEND_OFFER_DSTATE 
};
TraceEvent _TRACE_VMBUS_TERMINATE_OFFERS_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_terminate_offers",
    .sstate = TRACE_VMBUS_TERMINATE_OFFERS_ENABLED,
    .dstate = &_TRACE_VMBUS_TERMINATE_OFFERS_DSTATE 
};
TraceEvent _TRACE_VMBUS_GPADL_HEADER_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_gpadl_header",
    .sstate = TRACE_VMBUS_GPADL_HEADER_ENABLED,
    .dstate = &_TRACE_VMBUS_GPADL_HEADER_DSTATE 
};
TraceEvent _TRACE_VMBUS_GPADL_BODY_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_gpadl_body",
    .sstate = TRACE_VMBUS_GPADL_BODY_ENABLED,
    .dstate = &_TRACE_VMBUS_GPADL_BODY_DSTATE 
};
TraceEvent _TRACE_VMBUS_GPADL_CREATED_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_gpadl_created",
    .sstate = TRACE_VMBUS_GPADL_CREATED_ENABLED,
    .dstate = &_TRACE_VMBUS_GPADL_CREATED_DSTATE 
};
TraceEvent _TRACE_VMBUS_GPADL_TEARDOWN_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_gpadl_teardown",
    .sstate = TRACE_VMBUS_GPADL_TEARDOWN_ENABLED,
    .dstate = &_TRACE_VMBUS_GPADL_TEARDOWN_DSTATE 
};
TraceEvent _TRACE_VMBUS_GPADL_TORNDOWN_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_gpadl_torndown",
    .sstate = TRACE_VMBUS_GPADL_TORNDOWN_ENABLED,
    .dstate = &_TRACE_VMBUS_GPADL_TORNDOWN_DSTATE 
};
TraceEvent _TRACE_VMBUS_OPEN_CHANNEL_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_open_channel",
    .sstate = TRACE_VMBUS_OPEN_CHANNEL_ENABLED,
    .dstate = &_TRACE_VMBUS_OPEN_CHANNEL_DSTATE 
};
TraceEvent _TRACE_VMBUS_CHANNEL_OPEN_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_channel_open",
    .sstate = TRACE_VMBUS_CHANNEL_OPEN_ENABLED,
    .dstate = &_TRACE_VMBUS_CHANNEL_OPEN_DSTATE 
};
TraceEvent _TRACE_VMBUS_CLOSE_CHANNEL_EVENT = {
    .id = 0,
    .vcpu_id = TRACE_VCPU_EVENT_NONE,
    .name = "vmbus_close_channel",
    .sstate = TRACE_VMBUS_CLOSE_CHANNEL_ENABLED,
    .dstate = &_TRACE_VMBUS_CLOSE_CHANNEL_DSTATE 
};
TraceEvent *hw_hyperv_trace_events[] = {
    &_TRACE_VMBUS_RECV_MESSAGE_EVENT,
    &_TRACE_VMBUS_SIGNAL_EVENT_EVENT,
    &_TRACE_VMBUS_CHANNEL_NOTIFY_GUEST_EVENT,
    &_TRACE_VMBUS_POST_MSG_EVENT,
    &_TRACE_VMBUS_MSG_CB_EVENT,
    &_TRACE_VMBUS_PROCESS_INCOMING_MESSAGE_EVENT,
    &_TRACE_VMBUS_INITIATE_CONTACT_EVENT,
    &_TRACE_VMBUS_SEND_OFFER_EVENT,
    &_TRACE_VMBUS_TERMINATE_OFFERS_EVENT,
    &_TRACE_VMBUS_GPADL_HEADER_EVENT,
    &_TRACE_VMBUS_GPADL_BODY_EVENT,
    &_TRACE_VMBUS_GPADL_CREATED_EVENT,
    &_TRACE_VMBUS_GPADL_TEARDOWN_EVENT,
    &_TRACE_VMBUS_GPADL_TORNDOWN_EVENT,
    &_TRACE_VMBUS_OPEN_CHANNEL_EVENT,
    &_TRACE_VMBUS_CHANNEL_OPEN_EVENT,
    &_TRACE_VMBUS_CLOSE_CHANNEL_EVENT,
  NULL,
};

static void trace_hw_hyperv_register_events(void)
{
    trace_event_register_group(hw_hyperv_trace_events);
}
trace_init(trace_hw_hyperv_register_events)