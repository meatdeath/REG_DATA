/*****************************************************************************************************
 * :Project: RS-485 data registrator
 * :Author: Vladimir Novac
 * :Email: vladimir.novac.1980@gmail.com
 * :Date created: 26/12/2018
 * :Modified Date: 26/05/2020
 * :License: Public Domain
 ****************************************************************************************************/

#define VERSION       "1.61"

#define U_MODE 'U'
#define I_MODE 'I'
#define DEFAULT_MODE  I_MODE

//#define TEST

// ---------------------------------------------------------------------------------------------------

// Подключаем внешние библиотеки
#ifndef TEST
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TM1637Display.h>
//#include <ArduinoJson.h>
#endif

#include <DS3231.h>
#include <TimerOne.h>

// Подключаем файлы скетча
#include "buttons.h"


// ---------------------------------------------------------------------------------------------------
// Назначение пинов
// ---------------------------------------------------------------------------------------------------

// SD card CS pin definition
#define SPI_CS_PIN      10                          // SD card CS D10

// DS3231 clock generation output
#define SQW_PIN         2                           // DS3231 SQW D2

// 7-segments display module connection pins (Digital Pins)
#define DISPLAY_CLK     3                           // TM1637 SCK D3
#define DISPLAY_DIO     4                           // TM1637 DIO D4

// External Led pin & operation definition
#define LED_REC         7                           // LED D7
#define LED_WR          8                           // LED D8


// ---------------------------------------------------------------------------------------------------
// Макросы
// ---------------------------------------------------------------------------------------------------

#define FILE_NUMBER_MAX     1000
// Инверсия байт
#define htons(x)            ( ((x)<< 8 & 0xFF00) | ((x)>> 8 & 0x00FF) )
#define ntohs(x)            htons(x)

// Управление светодиодами
#define INFINITE_CYCLES     0xFF
#define led_init(pin)       pinMode(pin, OUTPUT)       // Инициализация LED
#define led_off(pin)        digitalWrite(pin, LOW)     // Погасить LED (LOW is the voltage level)
#define led_on(pin, cycles) do {digitalWrite(pin, HIGH); led_wr_timer = cycles;} while(0)    // Зажечь LED (HIGH is the voltage level) на cycles циклов таймера 1


#define DISPLAY_COLON   0xE0

// Для дисплея
//     A
//    ---
// F |   | B
//    -G-
// E |   | C
//    --- 
//     D

const uint8_t seg_digit[] = {
    (SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F      ),  // 0
    (SEG_B|      SEG_C                        ),  // 1
    (SEG_A|SEG_B|      SEG_D|SEG_E|      SEG_G),  // 2
    (SEG_A|SEG_B|SEG_C|SEG_D|            SEG_G),  // 3
    (SEG_B|      SEG_C|            SEG_F|SEG_G),  // 4
    (SEG_A|      SEG_C|SEG_D|      SEG_F|SEG_G),  // 5
    (SEG_A|      SEG_C|SEG_D|SEG_E|SEG_F|SEG_G),  // 6
    (SEG_A|SEG_B|SEG_C                        ),  // 7
    (SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G),  // 8
    (SEG_A|SEG_B|SEG_C|SEG_D|      SEG_F|SEG_G),  // 9
    (SEG_A|SEG_B|SEG_C|      SEG_E|SEG_F|SEG_G),  // A
    (            SEG_C|SEG_D|SEG_E|SEG_F|SEG_G),  // b
    (SEG_A|            SEG_D|SEG_E            ),  // C
    (      SEG_B|SEG_C|SEG_D|SEG_E|      SEG_G),  // d
    (SEG_A|            SEG_D|SEG_E|SEG_F|SEG_G),  // E
    (SEG_A|                  SEG_E|SEG_F|SEG_G),  // E
};

#define SEG_MINUS       SEG_G
#define SEG_LETTER_r    (SEG_E|SEG_G)
#define SEG_LETTER_u    (SEG_C|SEG_D|SEG_E)
#define SEG_LETTER_i    SEG_E
#define SEG_LETTER_E    seg_digit[0xE]
#define SEG_LETTER_C    seg_digit[0xC]

enum _enDisplayErrors {
    ERROR_INIT_SD_ERR = 0,
    ERROR_INIT_OSCILLATOR,
    ERROR_FILE_NUMBER,
    ERROR_WRITE_FILE,
    ERROR_MAX // maximum 9 supported
};


//----------------------------------------------------------------------------------------------------
// Константы
//----------------------------------------------------------------------------------------------------

// Состояние редактора часов
enum time_set_en {
    TIME_SET_IDLE = 0,
    TIME_SET_YEAR,
    TIME_SET_MONTH,
    TIME_SET_DAY,
    TIME_SET_HOUR,
    TIME_SET_MINUTE
};


//----------------------------------------------------------------------------------------------------
// Структуры данных
//----------------------------------------------------------------------------------------------------
#pragma pack(1)
typedef union {
    struct {
        uint16_t val1;      // Значение 1
        uint16_t val2;      // Значение 2
        uint8_t relay;
        uint16_t r_val1;
        uint16_t r_val2;
    } values;
    uint8_t buf[7];         // Буфер приема данных
    uint16_t iVal;
} content_t;
#pragma pack()
//----------------------------------------------------------------------------------------------------
// Глобальные переменные
//----------------------------------------------------------------------------------------------------

content_t content;
uint8_t content_byte_index; // Индекс буфера приема данных

char filename[14];          // Хранит имя файла для сохранения данных

uint8_t time_set_state;

bool sd_rec_enable;
volatile bool blink_time;
volatile bool blinked;

DS3231 Clock;
RTClib RTC;
DateTime time_to_set;
volatile uint32_t unix_time;
volatile bool time_print_time;
volatile bool time_print_point;
volatile uint8_t led_wr_timer;
uint16_t last_u_val1;
uint16_t last_u_val2;

uint8_t last_hour;

#ifndef TEST
TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
#endif

char log_str[65];

struct config_st {
    char regMode;
    uint16_t vMin;
    uint16_t vMax;
};

config_st config;                         // <- global configuration object
  
//----------------------------------------------------------------------------------------------------
// Вспомогательные функции
//----------------------------------------------------------------------------------------------------

// Настройка таймера для моргания в режиме установки времени
void blink_timer_start( void ) {
    blink_time = true;
    blinked = true;
    Timer1.initialize( 250000 ); // Период Т=250ms
    Timer1.attachInterrupt( timer_isr );
    Timer1.restart();
}

// Обработчик прерывания таймера Т=250мс
void timer_isr( void ) {
    blink_time = true;
    blinked = !blinked;
    if( led_wr_timer != INFINITE_CYCLES ) {
        if(led_wr_timer) led_wr_timer--;
        else led_off(LED_WR);
    }
}

// Обработчик прерывания тактовых импульсов сигнала часов
void sqw_isr( void ) {
    time_print_time = true;
    if ( digitalRead(SQW_PIN) ) {
        unix_time++;
        time_print_point = true;
    } else {
        time_print_point = false;
    }
}

// Изменение двухбайтового параметра даты/времени
void change_time_u16_value( uint16_t *value, uint16_t min, uint16_t max ) {
    if( *value >= min && *value < max ) (*value)++;
    else *value = min;
    Timer1.start();
    blink_time = true;
    blinked = true;
}

// Изменение обнобайтового параметра даты/времени
void change_time_u8_value( uint8_t *value, uint8_t min, uint8_t max ) {
    if( *value >= min && *value < max ) (*value)++;
    else *value = min;
    Timer1.start();
    blink_time = true;
    blinked = true;
}

// Смена изменяемого параметра даты/времени
void switch_time_state( uint8_t new_state ) {
    time_set_state = new_state;
    Timer1.start();
    blink_time = true;
    blinked = true;
}

#ifndef TEST
// Функция-колбэк вызывается каждый раз при создании файла для использования правильной даты создания/изменения
void fileDateTimeCb(uint16_t* date, uint16_t* time) {
    DateTime now = DateTime( unix_time );
    // return date using FAT_DATE macro to format fields
    *date = FAT_DATE( now.year(), now.month(), now.day() );
    // return time using FAT_TIME macro to format fields
    *time = FAT_TIME( now.hour(), now.minute(), now.second() );
}

void display_error(uint8_t err_code) {
    uint16_t led_delay = 100;
    uint8_t t_seg[4] = { SEG_LETTER_E, SEG_LETTER_r, SEG_LETTER_r , 0 };
    switch(err_code) {
        case ERROR_INIT_SD_ERR:
            led_delay = 100;
            t_seg[3] = display.encodeDigit(err_code);
            break;
        case ERROR_INIT_OSCILLATOR:
            led_delay = 250;
            t_seg[3] = display.encodeDigit(err_code);
            break;
        case ERROR_FILE_NUMBER:
            led_delay = 500;
            t_seg[3] = display.encodeDigit(err_code);
            break;
        case ERROR_WRITE_FILE:
            led_delay = 1000;
            t_seg[3] = display.encodeDigit(err_code);
            break;
    }
    //display.setSegments(t_seg);
    while (1) {
        led_on(LED_REC, INFINITE_CYCLES);
        display.setBrightness(0x0F);
        display.setSegments(t_seg);
        delay(led_delay);
        
        led_off(LED_REC);
        display.setBrightness(0x00);
        display.setSegments(t_seg);
        delay(led_delay);
    }
}
#endif

void CreateNewFile(void) {
    int i;
    for( i = 0; i < FILE_NUMBER_MAX; i++ ) 
    {
        // Генерируем имя файла в виде "reg_MXXX.csv", где M-режим работы, XXX=i -  номер файла
        sprintf( filename, "%c_REG%03d.CSV", config.regMode, i );
        if( !SD.exists(filename) ) 
        {
            // Файл с таким именем еще не существует, используем его
            break;
        }
    }
    if( i == FILE_NUMBER_MAX ) 
    {
        display_error(ERROR_FILE_NUMBER);
    }
    led_on(LED_REC, INFINITE_CYCLES);
    File myFile = SD.open( filename, FILE_WRITE );           // Открываем файл для записи (данные будут записываться в конец файла)
    if( myFile ) {
        myFile.print( "File: " );
        myFile.print( filename );
        myFile.print( " (" );
        myFile.print( VERSION );
        myFile.print( ")\n" );
        switch( config.regMode ) {
            case I_MODE: myFile.print( "Unix time,Date,Time,Alarm,Current I(A)\n" ); break;
            case U_MODE: myFile.print( "Unix time,Date,Time,U-,U+,K-,K+,R-,R+,Hex Dump\n" ); break;
            default:     myFile.print( "Error! Unknown mode!\n" );
        }
        myFile.close();   // Закрываем файл
    }
}

// -------------------------------------------------------------------------------------------------------------
// Начальная установка и настройка при старте МК
// -------------------------------------------------------------------------------------------------------------

void setup() {
    content.values.val1 = 0;                    // Очищаем содержимое всех значений
    content.values.val2 = 0;
    content_byte_index = 0;                     // Начинаем с приема первого байта
  
    led_init(LED_REC);                          // Инициализация LED (D7)
    led_off(LED_REC);
    led_init(LED_WR);                           // Инициализация LED (D8)
    led_off(LED_WR);
    led_wr_timer = 0;

    // Инициализируем кнопки
    buttons_init();                             // Инициализация пинов кнопок

    // Режим работы по умолчанию
    config.regMode = DEFAULT_MODE;
    // Если при старте нажата кнопка CHANGE(ИЗМЕНИТЬ), то меняем режим работы устройства
    if( digitalRead(BUTTON2_PIN) == BUTTON_ACTIVE ) {
        if( config.regMode == U_MODE ) {
            config.regMode = I_MODE;
        }
        else {
            config.regMode = U_MODE;
        }
    }
        
    if( config.regMode == I_MODE ) {
        config.vMax = 2000;
        config.vMin = 0;
    } else if( config.regMode == U_MODE ) {
        config.vMax = 824;
        config.vMin = 0;
    }

    //delay(1000);
  
    // Инициализация UART: скорость передачи данных 115200 бод
    Serial.begin( 115200 );
    // Таймаут приема данных функцией Serial.readBytes() настраиваем на 0.1 сек
    Serial.setTimeout( 100 );
    // Печатаем в UART о том, что начали работать
    sprintf( log_str, "Starting (version %s)...\n", VERSION );
    Serial.print(log_str);
    
    // Установка яркости дисплея
    display.setBrightness(0x0F);
    
    // Очистка дисплея
    display.clear();
    
    uint8_t t_seg[4] = { 
       (uint8_t)(config.regMode==U_MODE)?SEG_LETTER_u:SEG_LETTER_i, 
        display.encodeDigit(VERSION[0]-'0'), 
        display.encodeDigit(VERSION[2]-'0'), 
        display.encodeDigit(VERSION[3]-'0') };
    display.setSegments(t_seg);
            
    delay(2000);                                // Задержка в 2 сек для защиты от помех при включении

#ifndef TEST
  
    // Очистка дисплея
    display.clear();
    
    time_print_time = true;   // пришло время отобразить время на дислее
    time_print_point = true;  // время отображать двоеточие
  
#ifndef TEST_NO_SD
    // Задать функцию-колбэк для правильной даты создания/изменения файлов
    SdFile::dateTimeCallback( fileDateTimeCb );
  
    // Инициализируем SD карту
    //    Serial.println("Init SD card...");
    if ( !SD.begin(SPI_CS_PIN) ) {
      // Ошибка при инициализиации SD карты
      // Serial.println("Error while init SD card!");
      // Виснем моргая светодиодом c периодом 400мс (примерно 2 раза в сек)
      display_error(ERROR_INIT_SD_ERR);
    }
#endif

#endif
  
    // Режим установки времени не активен
    time_set_state = TIME_SET_IDLE;
  
    // Настройка таймера для моргания в режиме установки времени
    blink_timer_start();
    
    // Время по умолчанию: 2019/01/24  01:43:43
    unix_time = 1548294223; 
    
#ifndef TEST
    // Инициализация шины I2C
    Wire.begin();
    
    // Чтение времени
    unix_time = RTC.now().unixtime();
    
    // Запуск часов
    Clock.enableOscillator( true, false, 0 );
  
    // Serial.print( "Temperature: " );
    // Serial.print( Clock.getTemperature(), DEC );
    // Serial.println( "C" );
  
    // Инициализация пина куда приходит частота 1Гц с модуля часов
    pinMode( digitalPinToInterrupt(SQW_PIN), INPUT_PULLUP );
    attachInterrupt( digitalPinToInterrupt(SQW_PIN), sqw_isr, CHANGE );
  
//    // Проверяем запущены ли часы
//    if( Clock.oscillatorCheck() == false ) {
//        Serial.println("WARNING! Clock oscillator is not running!");
//        display_error(ERROR_INIT_OSCILLATOR);
//    }
#endif

    // автостарт записи
    sd_rec_enable = true;
    CreateNewFile();
    DateTime now = DateTime( unix_time );
    last_hour = now.hour();

    // Загрузка конфигурации из файла с SD карты
    // loadConfiguration();

    
    last_u_val1 = 0;
    last_u_val2 = 0;
    
    // Инициализация окончена
    Serial.println( "Initialization DONE." );
}


// -------------------------------------------------------------------------------------------------------------
// Функция вызывается постоянно во время работы МК
// -------------------------------------------------------------------------------------------------------------
void loop() {
    uint8_t button_set, button_change;

#ifndef TEST
    if( time_set_state != TIME_SET_IDLE ) {
        
        uint16_t year   = time_to_set.year();
        uint8_t  month  = time_to_set.month();
        uint8_t  day    = time_to_set.day();
        uint8_t  hour   = time_to_set.hour();
        uint8_t  minute = time_to_set.minute();
        switch ( time_set_state ) {
            case TIME_SET_YEAR:
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( year, 0, true, 4, 0 );
                    else display.clear();
                    blink_time = false;
                }
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    //change_time_u16_value( &year, 2000, 2100 );
                    if( year > 1999 && year < 2099 ) year++;
                    else year = 2000;
                    Timer1.start();
                    blink_time = true;
                    blinked = true;
                    time_to_set = DateTime( year, time_to_set.month(), time_to_set.day(), time_to_set.hour(), time_to_set.minute(), 0 );
                } else if( button_set && !button_change ) {
                    switch_time_state( TIME_SET_MONTH );
                }
                break;
                
            case TIME_SET_MONTH:
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( month, 0, true, 2, 0 );
                    else display.clear();
                    blink_time = false;
                }
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    change_time_u8_value( &month, 1, 12 );
                    time_to_set = DateTime( time_to_set.year(), month, time_to_set.day(), time_to_set.hour(), time_to_set.minute(), 0 );
                } else if( button_set && !button_change ) {
                    switch_time_state( TIME_SET_DAY );
                }
                break;
                
            case TIME_SET_DAY:
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( day, 0, true, 2, 2 );
                    else display.clear();
                    blink_time = false;
                }
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    change_time_u8_value( &day, 1, 31 );
                    time_to_set = DateTime( time_to_set.year(), time_to_set.month(), day, time_to_set.hour(), time_to_set.minute(), 0 );
                } else if ( button_set && !button_change ) {
                    switch_time_state( TIME_SET_HOUR );
                }
                break;
                
            case TIME_SET_HOUR:
                if( blink_time ) {
                    uint8_t set_minute = time_to_set.minute();
                    if( blinked ) {
                        uint16_t digits = ((uint16_t)hour) * 100 + set_minute;
                        display.showNumberDecEx(digits, DISPLAY_COLON, true, 4, 0);
                    } else {
                        display.clear();
                        display.showNumberDecEx(minute, DISPLAY_COLON, true, 2, 2);
                    }
                    blink_time = false;
                }
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    // change
                    change_time_u8_value( &hour, 0, 23 );
                    time_to_set = DateTime( time_to_set.year(), time_to_set.month(), time_to_set.day(), hour, time_to_set.minute(), 0 );
                } else if( button_set && !button_change ) {
                    // set
                    switch_time_state( TIME_SET_MINUTE );
                }
                break;
                
            case TIME_SET_MINUTE:
                if( blink_time ) {
                    uint8_t set_hour = time_to_set.hour();
                    if( blinked ) {
                        uint16_t digits = ((uint16_t)set_hour) * 100 + minute;
                        display.showNumberDecEx( digits, DISPLAY_COLON, true, 4, 0 );
                    } else {
                        display.clear();
                        display.showNumberDecEx( set_hour, DISPLAY_COLON, true, 2, 0 );
                    }
                    blink_time = false;
                }
                buttons_process( button_set, button_change );
                if ( !button_set && button_change ) {
                    // change
                    change_time_u8_value( &minute, 0, 59 );
                    time_to_set = DateTime( time_to_set.year(), time_to_set.month(), time_to_set.day(), time_to_set.hour(), minute, 0 );
                } else if ( button_set && !button_change ) {
                    // set
                    Clock.setYear(time_to_set.year()-2000);
                    Clock.setMonth(time_to_set.month());
                    Clock.setDate(time_to_set.day());
                    Clock.setHour(time_to_set.hour());
                    Clock.setMinute(time_to_set.minute());
                    Clock.setSecond(0);
                    unix_time = time_to_set.unixtime();
                    switch_time_state( TIME_SET_IDLE );
                }
                break;
                
            default:
                time_set_state = TIME_SET_IDLE;
        }
        return;
    } else {
#endif

        uint8_t read_len = 0;
        uint8_t context_size = 1;
        
        #define U_CONTENT_SIZE  5
        #define I_CONTENT_SIZE  2
        #define RELAY_1         (1<<0)
        #define RELAY_2         (1<<1)
        #define R_FLAG          (1<<4)

        switch( config.regMode ) {
            case U_MODE:    context_size = U_CONTENT_SIZE; break;
            case I_MODE:    context_size = I_CONTENT_SIZE; break;
            default:        context_size = 1; break;
        }
        
        // Читаем из порта нужное количество байт или выходим по таймауту
        read_len = Serial.readBytes( &content.buf[content_byte_index], context_size - content_byte_index );
        //read_len = Serial.readBytes( &content.buf[content_byte_index], 1 ); // Читаем из порта 1 байт или выходим по таймауту


    #ifndef TEST
        DateTime now = DateTime( unix_time );
        uint8_t reg_hour = now.hour();

        if( sd_rec_enable ) {              // Если разрешена запись на SD карту
            if( last_hour != reg_hour &&
                ( 
                    reg_hour == 0  || 
                    reg_hour == 5  || 
                    reg_hour == 10 || 
                    reg_hour == 15 || 
                    reg_hour == 20
                ) 
            ) {
                CreateNewFile();
            }
            last_hour = reg_hour;
        }
    #endif

        if( read_len ) {                                            // Если что-то считали из порта

            switch( config.regMode ) {

                case U_MODE:
                
                    led_on(LED_WR, 2);
                    content_byte_index += read_len;                         // Увеличиваем общее количество считанных байт
                    
                    if( content_byte_index == U_CONTENT_SIZE ) {           // Если данные готовы к сохранению
                        uint8_t content_hexes[5] = { content.buf[0], content.buf[1], content.buf[2], content.buf[3], content.buf[4] };

                        content.values.val1 = ntohs(content.values.val1);   // Переводим значения из сетевого формата в формат хранения в памяти
                        content.values.val2 = ntohs(content.values.val2);
            
                        if( content.values.relay&R_FLAG ) {                   // проверяем, являются ли данные сопротивлением изоляции
                            content.values.r_val1 = content.values.val1;    // заполняем ячейки сопротивления изоляции
                            content.values.r_val2 = content.values.val2;
                            content.values.val1 = last_u_val1;              // запоняем напряжение из предыдущих данных
                            content.values.val2 = last_u_val2;
                        } else {
                            last_u_val1 = content.values.val1;
                            last_u_val2 = content.values.val2;
                        }

                        if( content.values.val1 < config.vMax && 
                            content.values.val1 > config.vMin &&
                            content.values.val2 < config.vMax && 
                            content.values.val2 > config.vMin )
                        {
                
    #ifndef TEST
                            if( sd_rec_enable ) {              // Если разрешена запись на SD карту

                                File myFile = SD.open( filename, FILE_WRITE );           // Открываем файл для записи (данные будут записываться в конец файла)

                                if( myFile ) {
    #endif
                                    char r_str[13] = ",,";
                                    if( content.values.relay&R_FLAG ) {
                                        sprintf( 
                                            r_str,
                                            ",%d,%d",
                                            content.values.r_val1,
                                            content.values.r_val2
                                        );
                                    }

                                    // Сохраняем данные в файл
                                    sprintf( 
                                        log_str, 
                                        "%ld,%04d/%02d/%02d,%02d:%02d:%02d,%d,%d,%d,%d%s,%02X %02X %02X %02X %02X\n",
                                        unix_time, //10
                                        now.year(), now.month(), now.day(), //11
                                        now.hour(), now.minute(), now.second(), //9
                                        content.values.val1, content.values.val2,  //8
                                        (content.values.relay&RELAY_1)?1:0, //2
                                        (content.values.relay&RELAY_2)?1:0, //2
                                        r_str, //8
                                        content_hexes[0], //14
                                        content_hexes[1],
                                        content_hexes[2],
                                        content_hexes[3],
                                        content_hexes[4]
                                    );
    #ifndef TEST
                                    myFile.print( log_str );
                                    myFile.close();   // Закрываем файл
                                } 
                                //led_off(LED_WR);
                            } 
    #else
                            Serial.print( log_str );
    #endif
                    }
                    content_byte_index = 0;                             // Переходим в состояние приема первого байта
                    break;
                }


                case I_MODE:
        
                    switch(content_byte_index) {
                        case 0:
                            while( read_len && (content.buf[0]&0xF0) != 0 ) {
                                read_len--;
                                content.buf[0] = content.buf[1];
                            }
                            break;
                        case 1:
                            break;
                    }
                    content_byte_index += read_len;
                    if( content_byte_index >= I_CONTENT_SIZE ) {
                    
                        content.iVal = ntohs(content.iVal);
    #define IVALUE_MASK 0x7FF
                        if(  sd_rec_enable && 
                            (content.iVal&IVALUE_MASK) <= config.vMax && 
                            (content.iVal&IVALUE_MASK) >= config.vMin ) 
                        {              // Если разрешена запись на SD карту
                            led_on(LED_WR, INFINITE_CYCLES);
                            
                            DateTime now = DateTime( unix_time );

    #ifndef TEST
                            File myFile = SD.open( filename, FILE_WRITE );           // Открываем файл для записи (данные будут записываться в конец файла)
                            if( myFile ) 
                            {
    #endif
                                // Сохраняем данные в файл
                                sprintf( 
                                    log_str, 
                                    "%ld,%04d/%02d/%02d,%02d:%02d:%02d,%d,%d\n",
                                    unix_time,
                                    now.year(), now.month(), now.day(),
                                    now.hour(), now.minute(), now.second(),
                                    (content.iVal&0x0800)?1000:0, content.iVal&IVALUE_MASK
                                );
                                
    #ifndef TEST
                                myFile.print( log_str );
                                myFile.close();   // Закрываем файл
                            }
                            else
                            {
                                display_error(ERROR_WRITE_FILE);
                            }
    #else
                            Serial.print( log_str );
    #endif

                            led_off(LED_WR);
                        }

                        content_byte_index = 0;
                    }
                    break;
            }
        }

    #ifndef TEST
        else {
            content_byte_index = 0; // Таймаут! Переходим в состояние приема первого байта
            
            // Вывод времени на дислей
            if ( time_print_time ) {
                // Время изменилось
                time_print_time = false;
                // Готовим число для вывода времени
                uint16_t to_disp = ((uint16_t)now.hour()) * 100 + now.minute();
                // собственно вывод на дисплей
                if ( time_print_point == 0 ) {
                    display.showNumberDecEx(to_disp, DISPLAY_COLON, true, 4, 0);
                } else {
                    display.showNumberDecEx(to_disp, 0, true, 4, 0);
                }
            }
        
            buttons_process( button_set, button_change );
        
            if ( !sd_rec_enable && button_set && button_change ) {
                // Serial.print("\nTime set mode...\n");
                time_to_set = DateTime(unix_time);
                switch_time_state( TIME_SET_YEAR );
            }
            else if ( button_set && !button_change ) {
                // Нажата кнопка Пуск/Стоп
                sd_rec_enable = !sd_rec_enable;
                // Serial.print("\nSTART/STOP pressed.\n");
                if( sd_rec_enable ) 
                {
                    CreateNewFile();
                    last_hour = reg_hour;
                    last_u_val1 = 0;
                    last_u_val2 = 0;
                }
                else {
                    led_off(LED_REC);
                }
            } 
            else if ( button_change && !button_set ) {
                // Serial.print("CHANGE pressed.\n");
                float ft = Clock.getTemperature();
                int16_t temperature = ft;
                uint8_t t_seg[4] = { 0, 0, 0, display.encodeDigit(0xC) };
                if( temperature < 0 ) {
                    temperature = -temperature;
                    if( temperature < 10 )
                        t_seg[1] = SEG_MINUS;
                    else
                        t_seg[0] = SEG_MINUS;
                }
                t_seg[2] = display.encodeDigit(temperature%10); 
                if( temperature >= 10 )
                    t_seg[1] = display.encodeDigit(temperature/10);
                display.setSegments(t_seg);

                delay(3000);
            }
        }
    }
#endif
}
