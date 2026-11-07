#include "calibration_store.h"
#include "FatFs/ff.h"
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t magic,sequence,rotation;
    float coefficients[6];
    uint32_t checksum;
} CalibrationRecord;
_Static_assert(sizeof(CalibrationRecord)==40,"Calibration record format");
static FATFS volume;

static uint32_t checksum(const CalibrationRecord *record)
{
    const uint8_t *bytes=(const uint8_t *)record;
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<sizeof(*record)-4;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}

static bool valid(const CalibrationRecord *record)
{
    if(record->magic!=0x324c4353u || record->rotation!=0xe8 || record->checksum!=checksum(record))return false;
    for(unsigned i=0;i<6;++i)
        if(!(record->coefficients[i]>=-2000 && record->coefficients[i]<=2000))return false;
    float determinant=record->coefficients[0]*record->coefficients[4]-
                      record->coefficients[1]*record->coefficients[3];
    return determinant>1000 || determinant< -1000;
}

static bool read_slot(unsigned slot,CalibrationRecord *record)
{
    const char *path=slot?"0:/SYNTH/CAL1.BIN":"0:/SYNTH/CAL0.BIN";
    FIL file;UINT count=0;
    if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    FSIZE_t size=f_size(&file);
    FRESULT result=f_read(&file,record,sizeof(*record),&count);
    FRESULT close_result=f_close(&file);
    return result==FR_OK && close_result==FR_OK && count==sizeof(*record) &&
           size==sizeof(*record) && valid(record);
}

static int newest(CalibrationRecord *best)
{
    CalibrationRecord first,second;
    bool a=read_slot(0,&first),b=read_slot(1,&second);
    if(!a && !b)return -1;
    if(b && (!a || (int32_t)(second.sequence-first.sequence)>0)){*best=second;return 1;}
    *best=first;return 0;
}

bool calibration_load(float *coefficients)
{
    if(f_mount(&volume,"0:",1)!=FR_OK){
        f_mount(0,"0:",0);return false;
    }
    CalibrationRecord record;
    bool found=newest(&record)>=0;
    if(found)memcpy(coefficients,record.coefficients,sizeof(record.coefficients));
    f_mount(0,"0:",0);
    return found;
}

bool calibration_save(const float *coefficients)
{
    if(f_mount(&volume,"0:",1)!=FR_OK){
        f_mount(0,"0:",0);return false;
    }
    CalibrationRecord old,record={0};
    int current=newest(&old);
    record.magic=0x324c4353u;
    record.sequence=current<0?1:old.sequence+1;
    record.rotation=0xe8;
    memcpy(record.coefficients,coefficients,sizeof(record.coefficients));
    record.checksum=checksum(&record);
    bool saved=false;
    FRESULT directory=f_mkdir("0:/SYNTH");
    if(valid(&record) && (directory==FR_OK || directory==FR_EXIST)){
        FIL file;UINT written=0;
        const char *path=current==0?"0:/SYNTH/CAL1.BIN":"0:/SYNTH/CAL0.BIN";
        if(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK){
            FRESULT result=f_write(&file,&record,sizeof(record),&written);
            FRESULT synced=result==FR_OK?f_sync(&file):result;
            FRESULT closed=f_close(&file);
            saved=result==FR_OK && synced==FR_OK && closed==FR_OK && written==sizeof(record);
        }
    }
    f_mount(0,"0:",0);
    return saved;
}
