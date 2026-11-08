#include "tusb.h"
#include "synth_usb.h"

static const tusb_desc_device_t synth_device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,
    .bcdUSB=0x0200,.bDeviceClass=0,.bDeviceSubClass=0,.bDeviceProtocol=0,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4075,.bcdDevice=0x0103,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
};
const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&synth_device;
}

/* One UAC1 function with playback and capture streams, followed by USB MIDI. */
enum { AUDIO_LENGTH=165, TOTAL_LENGTH=9+AUDIO_LENGTH+TUD_MIDI_DESC_LEN };
static const uint8_t configuration[]={
    TUD_CONFIG_DESCRIPTOR(1,5,0,TOTAL_LENGTH,0,250),
    TUD_AUDIO10_DESC_STD_AC(0,0,4),
    TUD_AUDIO10_DESC_CS_AC(0x0100,42,1,2),
    TUD_AUDIO10_DESC_INPUT_TERM(1,AUDIO_TERM_TYPE_USB_STREAMING,0,1,0,0,0),
    TUD_AUDIO10_DESC_OUTPUT_TERM(2,AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER,0,1,0),
    TUD_AUDIO10_DESC_INPUT_TERM(3,AUDIO_TERM_TYPE_IN_GENERIC_MIC,0,1,0,0,0),
    TUD_AUDIO10_DESC_OUTPUT_TERM(4,AUDIO_TERM_TYPE_USB_STREAMING,0,3,0),
    /* Host -> STM32, clocked by USB frames. */
    TUD_AUDIO10_DESC_STD_AS_INT(1,0,0,0),
    TUD_AUDIO10_DESC_STD_AS_INT(1,1,1,0),
    TUD_AUDIO10_DESC_CS_AS_INT(1,1,AUDIO10_DATA_FORMAT_TYPE_I_PCM),
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(1,2,16,48000),
    TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x03,0x0d,96,1,0),
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(0,0,0),
    /* STM32 -> host: generated samples. */
    TUD_AUDIO10_DESC_STD_AS_INT(2,0,0,0),
    TUD_AUDIO10_DESC_STD_AS_INT(2,1,1,0),
    TUD_AUDIO10_DESC_CS_AS_INT(4,1,AUDIO10_DATA_FORMAT_TYPE_I_PCM),
    TUD_AUDIO10_DESC_TYPE_I_FORMAT(1,2,16,48000),
    TUD_AUDIO10_DESC_STD_AS_ISO_EP(0x82,0x0d,96,1,0),
    TUD_AUDIO10_DESC_CS_AS_ISO_EP(0,0,0),
    TUD_MIDI_DESCRIPTOR(3,5,0x01,0x81,64)
};
_Static_assert(sizeof(configuration)==TOTAL_LENGTH,"USB configuration length mismatch");
const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index,uint16_t language)
{
    (void)language;
    static uint16_t descriptor[40];
    static const char *strings[]={"","STM32 Lab","STM32 USB Synth","SYNTH0001","Synth Audio","Synth MIDI"};
    if(index==0){descriptor[0]=0x0304;descriptor[1]=0x0409;return descriptor;}
    if(index>=sizeof(strings)/sizeof(strings[0]))return 0;
    unsigned length=0;
    const char *text=strings[index];
    while(text[length] && length<39){descriptor[length+1]=(uint8_t)text[length];++length;}
    descriptor[0]=0x0300|(2*length+2);
    return descriptor;
}
