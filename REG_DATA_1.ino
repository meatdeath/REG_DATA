/*****************************************************************************************************
 * :Project: RS-485 data registrator
 * :Author: Vladimir Novac
 * :Email: vladimir.novac.1980@gmail.com
 * :Date created: 26/12/2018
 * :Modified Date: 24/03/2019
 * :License: Public Domain
 ****************************************************************************************************/

#define VERSION       "1.30"

// ---------------------------------------------------------------------------------------------------

// Подключаем внешние библиотеки
#include <SPI.h>
#include <SD.h>
#include <DS3231.h>
#include <Wire.h>
#include <TimerOne.h>
#include <TM1637Display.h>
// #include <EEPROM.h>

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

// Инверсия байт
#define htons(x)        ( ((x)<< 8 & 0xFF00) | ((x)>> 8 & 0x00FF) )
#define ntohs(x)        htons(x)

// Управление светодиодами
#define led_init(pin)   pinMode(pin, OUTPUT);       // Инициализация LED
#define led_off(pin)    digitalWrite(pin, LOW);     // Погасить LED (LOW is the voltage level)
#define led_on(pin)     digitalWrite(pin, HIGH);    // Зажечь LED (HIGH is the voltage level)

// Для дисплея
#define DISPLAY_COLON   0xE0
#define SEG_G           0b01000000
#define SEG_MINUS       SEG_G


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

typedef union {
  struct {
    uint16_t val1;      // Значение 1
    uint16_t val2;      // Значение 2
  } values;
  uint8_t buf[4];         // Буфер приема данных
} content_t;

//typedef union eeprom_item_u {
//    struct eeprom_item_s {
//        uint16_t file_number;
//        uint8_t valid;
//        uint8_t checksum;
//    } data;
//    uint32_t raw;
//    uint8_t raw_buf[4];
//} eeprom_item_t;


//----------------------------------------------------------------------------------------------------
// Глобальные переменные
//----------------------------------------------------------------------------------------------------

//eeprom_item_t eeprom_item;
//uint16_t eeprom_item_address;

content_t content;
uint8_t content_byte_index; // Индекс буфера приема данных

char filename[14];          // Хранит имя файла для сохранения данных
//bool new_file;

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

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);

char log_str[50];


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

// Функция-колбэк вызывается каждый раз при создании файла для использования правильной даты создания/изменения
void fileDateTimeCb(uint16_t* date, uint16_t* time) {
    DateTime now = DateTime( unix_time );
    // return date using FAT_DATE macro to format fields
    *date = FAT_DATE( now.year(), now.month(), now.day() );
  
    // return time using FAT_TIME macro to format fields
    *time = FAT_TIME( now.hour(), now.minute(), now.second() );
}

//void eeprom_erase()
//{
//    // Стираем все ячейки в EEPROM
//    for( uint16_t i = 0; (i+sizeof(eeprom_item_t)) <= EEPROM.length(); i += sizeof(eeprom_item_t) ) {
//        EEPROM.put( i, 0xFFFFFFFF );
//    }
//    // Текущее значение - пустая ячейка
//    eeprom_item.raw = 0xFFFFFFFF;
//    // Текущий адрес записи в EEPROM
//    eeprom_item_address = 0;
//}

// -------------------------------------------------------------------------------------------------------------
// Начальная установка и настройка при старте МК
// -------------------------------------------------------------------------------------------------------------

void setup() {
    uint16_t i;
  
    content.values.val1 = 0;                    // Очищаем содержимое всех значений
    content.values.val2 = 0;
    content_byte_index = 0;                     // Начинаем с приема первого байта
  
    led_init(LED_REC);                          // Инициализация LED (D7)
    led_off(LED_REC);
    led_init(LED_WR);                           // Инициализация LED (D8)
    led_off(LED_WR);
    led_wr_timer = 0;
  
    buttons_init();                             // Инициализация пинов кнопок
  
    delay(2000);                                // Задержка в 2 сек для защиты от помех при включении
  
    // Инициализация UART: скорость передачи данных 115200 бод
    Serial.begin( 115200 );
    // Таймаут приема данных функцией Serial.readBytes() настраиваем на 0.1 сек
    Serial.setTimeout( 100 );
    // Печатаем в UART о том, что начали работать
    
    
    sprintf( log_str, "Starting (version %s)...\n", VERSION );
    Serial.print(log_str);
  
    //    Serial.print( "Set display brightness");
    // Установка яркости дисплея
    display.setBrightness(0x0f);
  
    //    Serial.println( "Clear display");
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
      while (1) {
        for ( i = 0; i < 3 ; i++ ) {
          led_on(LED_REC);
          delay(200);
          led_off(LED_REC);
          delay(200);
        }
        delay(2000);
      }
    }
    //    Serial.println("SD init successfully.");
#endif
  
    // Режим установки времени не активен
    time_set_state = TIME_SET_IDLE;
  
    //		Serial.println("Init timer");
    // Настройка таймера для моргания в режиме установки времени
    blink_timer_start();
  
    //    Serial.println("Init DS3231");
    // Инициализация шины I2C
    Wire.begin();
    
    // Время по умолчанию: 2019/01/24  01:43:43
    unix_time = 1548294223; 
    
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
  
    // Проверяем запущены ли часы
    if( Clock.oscillatorCheck() == false ) {
        Serial.println("WARNING! Clock oscillator is not running!");
    }
  
//    // По умолчанию, если нет валидных ячеек, потребуется стирание
//    bool erase_required = true;
//    // Текущий адрес записи в EEPROM
//    eeprom_item_address = 0;
//    // Ищем либо свободную ячейку либо ячейку с неоконченной записью
//    for( i = 0; (i+sizeof(eeprom_item_t)) <= EEPROM.length(); i += sizeof(eeprom_item_t) ) {
//        // Читаем данные из ячейки EEPROM
//        EEPROM.get( i, eeprom_item.raw );
//        // Проверяем, найдена ли свободная ячейка
//        if( eeprom_item.raw == 0xFFFFFFFF ) {
//            // Стирание пока не требуется
//            erase_required = false;
//            // Сохраняем адрес текущей ячейки
//            eeprom_item_address = i;
//            // Прекращаем поиск ячейки
//            break;
//        }
//        // Проверяем, находится ли в ячейке данные о неоконченной записи
//        if( eeprom_item.data.valid == 0xFF && 
//            (eeprom_item.raw_buf[0] + eeprom_item.raw_buf[1] + eeprom_item.raw_buf[2] - eeprom_item.raw_buf[3]) == 0 
//        ) {
//            // Стирание пока не требуется
//            erase_required = false;
//            // Сохраняем адрес текущей ячейки
//            eeprom_item_address = i;
//            // Включаем запись
//            sd_rec_enable = true;
//            // Не создавать новый файл
//            new_file = false;
//            // Воссановить имя файла
//            sprintf( filename, "reg_%04d.csv", eeprom_item.data.file_number );
//            // Вкючить светодиод записи
//            led_on( LED_REC );
//            break;
//        }
//    }
//    // Проверяем, требуется ли стирание
//    if( erase_required ) 
//    {
//        eeprom_erase();
//    }
    
//    Serial.print("EEPROM addr: 0x");
//    Serial.print(eeprom_item_address,HEX);
//    Serial.print("  data: 0x");
//    Serial.print(eeprom_item.data.file_number,HEX);
//    Serial.println();
    
    // Инициализация окончена
    Serial.println( "Initialization DONE." );
}


// -------------------------------------------------------------------------------------------------------------
// Функция вызывается постоянно во время работы МК
// -------------------------------------------------------------------------------------------------------------
void loop() {
    uint16_t i;
    uint8_t button_set, button_change;
    
    uint16_t year  = time_to_set.year();
    uint8_t month  = time_to_set.month();
    uint8_t day    = time_to_set.day();
    uint8_t hour   = time_to_set.hour();
    uint8_t minute = time_to_set.minute();
  
    if( time_set_state != TIME_SET_IDLE ) {
        switch ( time_set_state ) {
            case TIME_SET_YEAR:
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
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( year, 0, true, 4, 0 );
                    else display.clear();
                    blink_time = false;
                }
                break;
            case TIME_SET_MONTH:
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    change_time_u8_value( &month, 1, 12 );
                    time_to_set = DateTime( time_to_set.year(), month, time_to_set.day(), time_to_set.hour(), time_to_set.minute(), 0 );
                } else if( button_set && !button_change ) {
                    switch_time_state( TIME_SET_DAY );
                }
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( month, 0, true, 2, 0 );
                    else display.clear();
                    blink_time = false;
                }
                break;
            case TIME_SET_DAY:
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    change_time_u8_value( &day, 1, 31 );
                    time_to_set = DateTime( time_to_set.year(), time_to_set.month(), day, time_to_set.hour(), time_to_set.minute(), 0 );
                } else if ( button_set && !button_change ) {
                    switch_time_state( TIME_SET_HOUR );
                }
                if( blink_time ) {
                    if( blinked ) display.showNumberDecEx( day, 0, true, 2, 2 );
                    else display.clear();
                    blink_time = false;
                }
                break;
            case TIME_SET_HOUR:
                buttons_process( button_set, button_change );
                if( !button_set && button_change ) {
                    // change
                    change_time_u8_value( &hour, 0, 23 );
                    time_to_set = DateTime( time_to_set.year(), time_to_set.month(), time_to_set.day(), hour, time_to_set.minute(), 0 );
                } else if( button_set && !button_change ) {
                    // set
                    switch_time_state( TIME_SET_MINUTE );
                }
                if( blink_time ) {
                    uint8_t minute = time_to_set.minute();
                    if( blinked ) {
                        uint16_t digits = ((uint16_t)hour) * 100 + minute;
                        display.showNumberDecEx(digits, DISPLAY_COLON, true, 4, 0);
                    } else {
                        display.clear();
                        display.showNumberDecEx(minute, DISPLAY_COLON, true, 2, 2);
                    }
                    blink_time = false;
                }
                break;
            case TIME_SET_MINUTE:
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
                if( blink_time ) {
                    uint8_t hour = time_to_set.hour();
                    if( blinked ) {
                        uint16_t digits = ((uint16_t)hour) * 100 + minute;
                        display.showNumberDecEx( digits, DISPLAY_COLON, true, 4, 0 );
                    } else {
                        display.clear();
                        display.showNumberDecEx( hour, DISPLAY_COLON, true, 2, 0 );
                    }
                    blink_time = false;
                }
                break;
            default:
                time_set_state = TIME_SET_IDLE;
        }
        return;
    }

    uint8_t read_len = Serial.readBytes( &content.buf[content_byte_index], sizeof(content) - content_byte_index ); // Читаем из порта нужное количество байт или выходим по таймауту
  
    if( read_len ) {                                        // Если что-то считали из порта
        content_byte_index += read_len;                       // Увеличиваем общее количество считанных байт
        if( content_byte_index == sizeof(content) ) {         // Если данные готовы к сохранению
            content.values.val1 = ntohs(content.values.val1);   // Переводим значения из сетевого формата в формат хранения в памяти
            content.values.val2 = ntohs(content.values.val2);

//            if( !( content.values.val1 > 130 || content.values.val1 < 1 ||
//                   content.values.val2 > 130 || content.values.val2 < 1 )  )
            {
        
                // sprintf( log_str, "Unix Time: %ld\nData: %d, %d\n", unix_time, content.values.val1, content.values.val2 );
                // Serial.print( log_str );
      
                if( sd_rec_enable ) {              // Если разрешена запись на SD карту
                    led_on(LED_WR);
                    led_wr_timer = 5;
                    // Serial.println("Pause before saving data to SD...");
                    // delay(2000);                                    // Задержка в 2 сек для защиты от помех при выключении
                    // Serial.println( "Recording activated" );
                    DateTime now = DateTime( unix_time );
                    //if( new_file ) {
    //                if( eeprom_item.raw == 0xFFFFFFFF )
    //                {
    //                    for ( i = 0; i < 10000; i++ ) {
    //                        // Генерируем имя файла в виде "reg_XXXX.csv", где XXXX=i
    //                        sprintf( filename, "reg_%04d.csv", i );
    //                        if ( !SD.exists(filename) ) {
    //                            // Файл с таким именем еще не существует, используем его
    //                            break;
    //                        }
    //                    }
    //                    if ( i == 10000 ) {
    //                        // Нет свободных имен файлов
    //                        // Serial.println("Error! No more file names! Please remove files from SD card.");
    //                        // Виснем моргая светодиодом c периодом 1сек
    //                        while (1) {
    //                            for ( i = 0; i < 3 ; i++ ) {
    //                                led_on(LED_REC);
    //                                delay(500);
    //                                led_off(LED_REC);
    //                                delay(500);
    //                            }
    //                            delay(2000);
    //                        }
    //                    }
    //                    Serial.print( "Store recording info at address: 0x" );
    //                    Serial.print( eeprom_item_address, HEX );
    //                    Serial.println();
    //                    eeprom_item.data.file_number = i;
    //                    eeprom_item.data.valid = 0xFF;
    //                    eeprom_item.data.checksum = (i>>8) + (i&0xFF) + 0xFF;
    //                    EEPROM.put( eeprom_item_address, eeprom_item.raw );
    //                } 
    //                
    //                sprintf( filename, "reg_%04d.csv", eeprom_item.data.file_number );
                    File myFile = SD.open( filename, FILE_WRITE );           // Открываем файл для записи (данные будут записываться в конец файла)
                    if( myFile ) 
                    {
    //                    if( eeprom_item.raw == 0xFFFFFFFF ) 
    //                    {
    //                        myFile.print( "Unix time,Date,Time,Resistanse -,Resistanse +\n" );
    //                    }
                        // new_file = false;
                        
                        // Сохраняем данные в файл
                        sprintf( 
                            log_str, 
                            "%ld,%04d/%02d/%02d,%02d:%02d:%02d,%d,%d\n",
                            unix_time,
                            now.year(), now.month(), now.day(),
                            now.hour(), now.minute(), now.second(),
                            content.values.val1, content.values.val2
                        );
                        myFile.print( log_str );
                        
                        myFile.close();   // Закрываем файл
          
                    } else {
                        //Serial.print( "Error! Can't open file!\n" );
                    }
                    led_off(LED_WR);
                } else {
                    //Serial.print( "Recording is not active. Data SKIPPED!\n" );
                }
            }
            content_byte_index = 0;                             // Переходим в состояние приема первого байта
        }
    }
    else
    {
        content_byte_index = 0; // Таймаут! Переходим в состояние приема первого байта
        //Serial.print(".");
        
        // Вывод времени на дислей
        if ( time_print_time ) {
            // Время изменилось
            time_print_time = false;
            // Готовим массив чисел для вывода времени
            DateTime now = DateTime(unix_time);
        
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
                led_on(LED_REC);
                //if( eeprom_item.raw == 0xFFFFFFFF )
                {
                    for( i = 0; i < 10000; i++ ) 
                    {
                        // Генерируем имя файла в виде "reg_XXXX.csv", где XXXX=i
                        sprintf( filename, "reg_%04d.csv", i );
                        if( !SD.exists(filename) ) 
                        {
                            // Файл с таким именем еще не существует, используем его
                            break;
                        }
                    }
                    if( i == 10000 ) 
                    {
                        // Нет свободных имен файлов
                        // Serial.println("Error! No more file names! Please remove files from SD card.");
                        // Виснем моргая светодиодом c периодом 1сек
                        while(1) 
                        {
                            for( i = 0; i < 3 ; i++ ) 
                            {
                                led_on(LED_REC);
                                delay(500);
                                led_off(LED_REC);
                                delay(500);
                            }
                            delay(2000);
                        }
                    }
                    File myFile = SD.open( filename, FILE_WRITE );           // Открываем файл для записи (данные будут записываться в конец файла)
                    if( myFile ) {
                        myFile.print( "Unix time,Date,Time,Resistanse -,Resistanse +\n" );
                        myFile.close();   // Закрываем файл
                    }
//                    eeprom_item.data.file_number = i;
//                    eeprom_item.data.valid = 0xFF;
//                    eeprom_item.data.checksum = (i>>8) + (i&0xFF) + 0xFF;
//                    Serial.print( "Store recording info at address: 0x" );
//                    Serial.print( eeprom_item_address, HEX );
//                    Serial.println();
//                    EEPROM.put( eeprom_item_address, eeprom_item.raw );
                }
            }
            else {
                led_off(LED_REC);
        
                // if( new_file == false ) 
//                {
//                    EEPROM[eeprom_item_address+2] = 0x00; // invalidate item
//                }
//                eeprom_item_address += sizeof(eeprom_item_t);
//                if( (eeprom_item_address+sizeof(eeprom_item_t)) >= EEPROM.length() )
//                {
//                    eeprom_erase();
//                }
                // new_file = true;
            }
        } else if ( button_change && !button_set ) {
            // Serial.print("CHANGE pressed.\n");
            float ft = Clock.getTemperature();
            uint16_t temperature = ft;
            // sprintf( log_str, "Temperature: %ud C\n", temperature );
            // Serial.print( log_str );
//            uint8_t seg[4] = { 0, 0, 0, display.encodeDigit(0xC) };
//            uint8_t pos = 2;
//            seg[pos--] = display.encodeDigit(temperature%10); 
//            if( temperature < -9 || temperature > 9 )
//                seg[pos--] = display.encodeDigit(temperature/10);
//            if( temperature < 0 )
//                seg[pos] = SEG_MINUS;
            display.showNumberDecEx(temperature, 0, true, 4, 0);
            delay(2000);
        }
    }
}
