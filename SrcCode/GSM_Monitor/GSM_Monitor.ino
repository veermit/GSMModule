#include <Relay.h>
#include <SoftwareSerial.h>

/* control macros */
#define PRINT_DEBUG
#define ENABLE_WARNING

/* pin mappings */
#define RELAY_OUT_PIN       12  
#define RELAY_FEDBK_PIN     A0
#define BATT_MON_PIN        A2
#define PULSE_SENSE_PIN     2 // or 3
#define GSM_TX_PIN          9
#define GSM_RX_PIN          10
#define GSM_POWER_KEY       11

/* command strings and IDs */
#define GSM_RING_STR                            "\r\nRING"
#define GSM_CALL_ID_STR                         "\r\n+CLIP"
#define GSM_POWER_DOWN_STR                      "NORMAL POWER DOWN"

#define CMD_INVALID_CMD_ID                      (-1)
#define CMD_GSM_CALL_RECV_ID                    0x01
#define CMD_GSM_INVALID_CALL_RECV_ID            0x02
#define CMD_GSM_POWER_DOWN_ID                   0x03

/* warnings */
#define OFF_STATE_WARNING                       0
#define LOW_BATT_WARNING                        1
#define SENSE_WARNING                           2
#define MAX_WARNING_COUNT                       3

/* system macros */
#define MAX_DEBUG_MSG_SIZE                      128
#define MAX_CMD_STRING_SIZE                     128

#define GSM_BAUDRATE                            9600
#define DEBUG_BAUDRATE                          9600
#define GSM_SERIAL_READ_DELAY_MS                0x02

/* sense pin event types */
#define SENSE_EVENT_PULSE_COUNT                 0x01
#define SENSE_EVENT_LOW_TO_HIGH                 0x02
#define SENSE_EVENT_HIGH_TO_LOW                 0x03
#define SELECTED_SENSE_EVENT                    SENSE_EVENT_LOW_TO_HIGH

// ATDxxxxxxxxxx; -- watch out here for semicolon at the end!!
// CLIP: "+916384215939",145,"",,"",0"
#define GSM_CONTACT_NUMBER_1                    "9940398991" 
#define GSM_CONTACT_NUMBER_2                    "9543807286"
#define GSM_CONTACT_NUMBER_3                    "9880303867"
#define MAX_CONTACT_NUMBERS_STORED              3

#define MAX_OFFSTATE_TIME_SECONDS               (1800UL)
#define LOW_BATT_THRESHOLD                      200
#define SENSE_MONITOR_PERIOD_SEC                10
#define SENSE_PULSE_PER_PERIOD                  5
#define CALL_TIMEOUT_SEC                        10
#define LOW_BAT_MAX_ADC_SAMPLES                 5
#define GSM_POWER_KEY_PULSE_TIME_MS             (2000)

/* warning timeouts in minutes */
#define OFFSTATE_WARNING_PERIOD_MIN             (30)
#define LOWBAT_WARNING_PERIOD_MIN               (30)
#define SENSE_WARNING_PERIOD_MIN                (2)

Relay Rly(RELAY_OUT_PIN, RELAY_ON, true /* active low is true */);
SoftwareSerial SS_GSM(GSM_TX_PIN, GSM_RX_PIN);

uint8_t g_CurState = Rly.getState();
uint8_t g_PreState = Rly.getState();

unsigned long g_ulOFFTime_ms = 0;
unsigned long g_ulStartime_ms = 0;
unsigned long g_ulWarStartTime_ms[MAX_WARNING_COUNT] = {0};

/* set this to true by default */
int g_iSendWarning[MAX_WARNING_COUNT] = {true, true, true};

#ifdef PRINT_DEBUG
  char g_arrcMsg[MAX_DEBUG_MSG_SIZE] = {0};
#endif

char g_arrcGSMMsg[MAX_CMD_STRING_SIZE] = {0};
char g_arrcMsgTxt[MAX_CMD_STRING_SIZE] = {0};

/* pulse count */
volatile unsigned long g_vulPulseCount = 0;
/* initializing to HIGH as this pin will be pulled up */
byte g_PrePinState = HIGH;
unsigned long g_ulPreTime = 0;

/* contact numbers */
char ContactNumbers[MAX_CONTACT_NUMBERS_STORED][11] = {GSM_CONTACT_NUMBER_1, GSM_CONTACT_NUMBER_2, GSM_CONTACT_NUMBER_3};
uint8_t g_MatchIndex = 0; 

/***********************************************************************************************/
/*! 
* \fn         :: setup()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function preforms the one time initializations
* \param[in]  :: None
* \return     :: None
*/
/***********************************************************************************************/
void setup() {
 
  /* set the sense pin to input pullup */
  pinMode(PULSE_SENSE_PIN, INPUT_PULLUP);
  
  /* intialize pulse sense pin for interrupt */
  // attachInterrupt(digitalPinToInterrupt(PULSE_SENSE_PIN), PulseSense_ISR, CHANGE);

  /* init GSM module */
  SS_GSM.begin(GSM_BAUDRATE);

  /* Enable caller ID */
  EnableCallerId(true);

  /* intialize GSM serial port */
  #ifdef PRINT_DEBUG
    /* initalize debug port */
    Serial.begin(DEBUG_BAUDRATE);
  #endif

  /* set the GSM_POWER_KEY to output */
  /* the GSM module needs a LOW 
    pulse on GSM_POWER_KEY for 2 seconds 
    every startup */
  pinMode(GSM_POWER_KEY, OUTPUT);
  GSM_PowerUpDown();

  #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "INIT Success");
      Serial.println(g_arrcMsg);
  #endif

}

/***********************************************************************************************/
/*! 
* \fn         :: EmgStopInterrupt()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This main loop function that performs the following tasks
*                 1. GSM Command handling
*                 2. OFF State detection
*                 3. Battery Low volatge detection
*                 4. Sense pin monitoring
* \param[in]  :: None
* \return     :: None
*/
/***********************************************************************************************/
void loop() {
  
  char arrcCmd[MAX_CMD_STRING_SIZE] = {0};
  int iReadBytes = 0;
  int iCmdID = 0;

  /* receive and process GSM commands */
  iReadBytes = RecvCmd(arrcCmd, MAX_CMD_STRING_SIZE); 
  if(iReadBytes > 0)
  {
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Received: [%d] %s", iReadBytes, arrcCmd);
      Serial.println(g_arrcMsg);
    #endif

    /* print bytes */
    // printBytes(arrcCmd, iReadBytes);

    // validate the command
    if(isValidCmd(arrcCmd, iReadBytes, &iCmdID) == true)
    {
      // if valid command is received, process it
      CmdProcess(iCmdID, g_arrcGSMMsg);
      
      // SS_GSM.println(g_arrcGSMMsg);
    }
    else
    {
      // do nothing
    }
  }

#if 1
  /* off state detection */
  if(detectOFFState(MAX_OFFSTATE_TIME_SECONDS))
  {
    ProcessWarning(OFF_STATE_WARNING);
  }

  /* Low battery detection */
  if(detectLowBatt())
  {
    ProcessWarning(LOW_BATT_WARNING);
  }
#endif

  /* Sense Pin detection */
  if(detectSensePin(SELECTED_SENSE_EVENT))
  {
    ProcessWarning(SENSE_WARNING);
  }

  // delay(1000);
}


/***********************************************************************************************/
/*! 
* \fn         :: PulseSense_ISR()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function increment the global pulse count for every pulse received
* \param[in]  :: None
* \return     :: None
*/
/***********************************************************************************************/
void PulseSense_ISR()
{
  /* increment the count for every pulse received */
  g_vulPulseCount++;
}

/***********************************************************************************************/
/*! 
* \fn         :: RecvCmd()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function cheks GSM serial port for received data and if data is available
*                then reads it, the number of bytes read is returned.
* \param[in]  :: pBuff, iBuflen
* \return     :: iIndex
*/
/***********************************************************************************************/
int RecvCmd(char *pBuff, int iBuflen)
{
  int iIndex = 0;

  if(pBuff == NULL)
  {
    return -1;
  }
  
  while(iIndex < iBuflen)
  {
    delay(GSM_SERIAL_READ_DELAY_MS);
    
    if(SS_GSM.available())
    {
      pBuff[iIndex] = SS_GSM.read();
      iIndex++;
    }
    else
    {
      break;
    }
  }

  return iIndex;
}

/***********************************************************************************************/
/*! 
* \fn         :: isValidCmd()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function validates the received command and return true if it a valid 
*                command else returns false, if a valid command is received, then the 
*                corresponding command ID is returned through out_iCmdID parameter, similarly
*                Invalid command ID error code is returned.
* \param[in]  :: parrcCmd, iCmdLen 
* \param[out] :: out_iCmdID
* \return     :: true or false
*/
/***********************************************************************************************/
bool isValidCmd(char *parrcCmd, int iCmdLen, int *out_iCmdID)
{
  int iRetVal = 0;

  if((parrcCmd == NULL) || (out_iCmdID == NULL) || (iCmdLen <= 0))
  {
    return false;
  }

  /* for every call GSM_RING_STR will received followed by GSM_CALL_ID_STR 
  in some cases, the both strings would be received as single message and in other
  cases both will be received as seperate strings, we only need GSM_CALL_ID_STR string 
  to validate the caller ID, hence if both strings are received as seperate messages, 
  we could process the GSM_CALL_ID_STR and discard the GSM_RING_STR, but when they are
  received as a single mesage, then we need to process the GSM_RING_STR also hence a
  case for GSM_RING_STR is added after GSM_CALL_ID_STR */

  if (StrnCmp(parrcCmd, GSM_CALL_ID_STR, strlen(GSM_CALL_ID_STR)) == true)
  {
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Call Received");
      Serial.println(g_arrcMsg);
    #endif

    /* validate caller */
    if(isVaildCaller(parrcCmd, iCmdLen))
    {
      *out_iCmdID = CMD_GSM_CALL_RECV_ID;
    }
    else
    {
      *out_iCmdID = CMD_GSM_INVALID_CALL_RECV_ID;
    }
    
    return true;
  }
  else if (StrnCmp(parrcCmd, GSM_RING_STR, strlen(GSM_RING_STR)) == true)
  {
    /* GSM_RING_STR followed by \r\n - 8 chars 
       GSM_CALL_ID_STR followed by contact number - 13 + 10 chars
       total =  31 chars [followed by additional info] */
      if(iCmdLen < 31)
      {
        #ifdef PRINT_DEBUG
          snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Inavlid String: %s", parrcCmd);
          Serial.println(g_arrcMsg);
        #endif

        *out_iCmdID = CMD_GSM_INVALID_CALL_RECV_ID;
      }
      else
      {
        #ifdef PRINT_DEBUG
          snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Call Received");
          Serial.println(g_arrcMsg);
        #endif

        /* validate caller */
        /* remove the eight bytes ie GSM_RING_STR followed by \r\n 
           and pass the string */
        if(isVaildCaller(&parrcCmd[8], iCmdLen))
        {
          *out_iCmdID = CMD_GSM_CALL_RECV_ID;
        }
        else
        {
          *out_iCmdID = CMD_GSM_INVALID_CALL_RECV_ID;
        }
      }
      
      return true;
  }
  else if(detectGSMPowerDown(parrcCmd, iCmdLen))
  {
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Power Down Cmd: %s", parrcCmd);
      Serial.println(g_arrcMsg);
    #endif
    *out_iCmdID = CMD_GSM_POWER_DOWN_ID;
    return true;
  }
  else
  {
    // invalid command
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE,"Invalid Cmd: %s", parrcCmd);
      Serial.println(g_arrcMsg);
    #endif
    *out_iCmdID = CMD_INVALID_CMD_ID;
  }

  return false;
}
/***********************************************************************************************/
/*! 
* \fn         :: isValidCmd()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function validates the caller's mobile number and returns true if the 
*                number is valid else returns false
* \param[in]  :: parrcCmd, iCmdLen 
* \param[out] :: None
* \return     :: true or false
*/
/***********************************************************************************************/
bool isVaildCaller(char *parrcCmd, int iCmdLen)
{
  int iIndex = 0;
  int iRet = 0;

  for(iIndex = 0; iIndex < MAX_CONTACT_NUMBERS_STORED; iIndex++)
  {
    // iRet = snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "\r\n+CLIP: "+91%snprintf",145,"",,"",0", ContactNumbers);
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE,"Cmp: %s : %s", parrcCmd, ContactNumbers[iIndex]);
      Serial.println(g_arrcMsg);
    #endif
    if (StrnCmp(&parrcCmd[13], ContactNumbers[iIndex], 10) == true)
    {
      g_MatchIndex = iIndex;
      #ifdef PRINT_DEBUG
        snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE,"Match found");
        Serial.println(g_arrcMsg);
      #endif
      
      return true;
    }
  }
  return false;
}

/***********************************************************************************************/
/*! 
* \fn         :: StrnCmp()
* \author     :: Vignesh S
* \date       :: 05-DEC-2018
* \brief      :: This function compares two strings, returns true if they are identical else
*                false.  
* \param[in]  :: pString1, pString2, iLen 
* \return     :: true or false
*/
/***********************************************************************************************/
bool StrnCmp(char *pString1, char *pString2, int iLen)
{
  if((pString1 == NULL) || (pString2 == NULL) || (iLen <= 0))
  {
    return false;
  }

  for(int iIndex = 0; (iIndex < iLen); iIndex++)
  {
    if(pString1[iIndex] != pString2[iIndex])
    {
      return false;
    }
  }

  return true;
}

/***********************************************************************************************/
/*! 
* \fn         :: CmdProcess()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function processes the recceived command and preforms corresponding task
* \param[in]  :: iCmdID
* \param[out] :: ipResponse
* \return     :: None
*/
/***********************************************************************************************/
void CmdProcess(int iCmdID, char *pResponse)
{ 
  int iRetVal = 0;

  if(pResponse == NULL)
  {
    return;
  }

  switch(iCmdID)
  {
    case CMD_GSM_CALL_RECV_ID:
      // sprintf(pResponse, "%s", "Response");
      /* don't answer the call just hangup */
      HangupCall();
      #ifdef PRINT_DEBUG
        snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Valid Call Toggling RELAY");
        Serial.println(g_arrcMsg);
      #endif
      
      Rly.ToggleState();
      
      /* send message */
      if(Rly.getState() == RELAY_ON)
      {
        snprintf(g_arrcMsgTxt, MAX_CMD_STRING_SIZE, "%s", "System is ON");
      }
      else
      {
        snprintf(g_arrcMsgTxt, MAX_CMD_STRING_SIZE, "%s", "System is OFF");
      }
      
      SendMessage(ContactNumbers[g_MatchIndex], g_arrcMsgTxt);

    break;

    case CMD_GSM_POWER_DOWN_ID:

      /* power up the GSM module */
      #ifdef PRINT_DEBUG
        snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "GSM Power Down detected");
        Serial.println(g_arrcMsg);
      #endif

      /* power up the GSM module */
      #ifdef PRINT_DEBUG
        snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Powering up GSM module");
        Serial.println(g_arrcMsg);
      #endif

      GSM_PowerUpDown();

    break;


    case CMD_GSM_INVALID_CALL_RECV_ID:
      /* don't answer the call just hangup */
      HangupCall();

      #ifdef PRINT_DEBUG
        snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Invalid Call Hanging up");
        Serial.println(g_arrcMsg);
      #endif
    break;

    default:
      ;// do nothing 
  }
}

/***********************************************************************************************/
/*! 
* \fn         :: detectOFFState()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function processes the recceived command and preforms corresponding task
* \param[in]  :: ulOFFTime_Sec OFF time threshold in seconds
* \param[out] :: None
* \return     :: true | false
*/
/***********************************************************************************************/
bool detectOFFState(unsigned long ulOFFTime_Sec)
{
  bool OFFState = false;

  /* update current state */
  g_CurState = Rly.getState();

  /* start the timer on ON to OFF Transition */
  if((g_PreState == RELAY_ON) && (g_CurState == RELAY_OFF))
  {
    g_ulStartime_ms = millis();
  }
  /* stop the timer on OFF to ON State Transition */
  /* clear the off time count */
  else if((g_PreState == RELAY_OFF) && (g_CurState == RELAY_ON))
  {
    g_ulOFFTime_ms = 0;
  }
  /* if the relay is OFF contineously then update off time */
  else if((g_PreState == RELAY_OFF) && (g_CurState == RELAY_OFF))
  {
    g_ulOFFTime_ms = millis() - g_ulStartime_ms;
  }
  else
  {
    // do nothing
  }

  /* If off time is greater than g_ulOFFTime_ms then */
  if((g_ulOFFTime_ms / 1000) > ulOFFTime_Sec)
  {
    OFFState = true;
  }

  /* assign current state to previous state */
  g_PreState = g_CurState;

  return OFFState;
}

/***********************************************************************************************/
/*! 
* \fn         :: ProcessWarning()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function processes warnings and initiates a call to GSM_CONTACT_NUMBER
*                and also sends warning messages
* \param[in]  :: iWarnID
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void ProcessWarning(int iWarnID)
{
  switch(iWarnID)
  {
    case OFF_STATE_WARNING:
      snprintf(g_arrcMsgTxt, MAX_CMD_STRING_SIZE, "System is OFF for more than %d minutes", 
              (MAX_OFFSTATE_TIME_SECONDS / 60));
      /* send consecutive warnings with atleast WARNING_PERIOD_MIN  interval inbetween */
      
      /* process OFF state warning */
      if((g_iSendWarning[OFF_STATE_WARNING] == false) && ((millis() - g_ulWarStartTime_ms[OFF_STATE_WARNING]) / (1000UL * 60UL) > OFFSTATE_WARNING_PERIOD_MIN))
      {
        g_iSendWarning[OFF_STATE_WARNING] = true;
      }
      
      if(g_iSendWarning[OFF_STATE_WARNING] == true)
      {
        SendWarning();

        g_iSendWarning[OFF_STATE_WARNING] = false;
        g_ulWarStartTime_ms[OFF_STATE_WARNING] = millis();
      }
    break;

    case LOW_BATT_WARNING:
      snprintf(g_arrcMsgTxt, MAX_CMD_STRING_SIZE, "Low Battery WARNING...!");
      
      /* process Low battery warning */
      if((g_iSendWarning[LOW_BATT_WARNING] == false) && ((millis() - g_ulWarStartTime_ms[LOW_BATT_WARNING]) / (1000UL * 60UL) > LOWBAT_WARNING_PERIOD_MIN))
      {
        g_iSendWarning[LOW_BATT_WARNING] = true;
      }
      
      if(g_iSendWarning[LOW_BATT_WARNING] == true)
      {
        SendWarning();

        g_iSendWarning[LOW_BATT_WARNING] = false;
        g_ulWarStartTime_ms[LOW_BATT_WARNING] = millis();
      }
    break;

    case SENSE_WARNING:
      snprintf(g_arrcMsgTxt, MAX_CMD_STRING_SIZE, "Sense Input WARNING...!");

      /* process Sense warning */
      if((g_iSendWarning[SENSE_WARNING] == false) && ((millis() - g_ulWarStartTime_ms[SENSE_WARNING]) / (1000UL * 60UL) > SENSE_WARNING_PERIOD_MIN))
      {
        g_iSendWarning[SENSE_WARNING] = true;
      }
      
      if(g_iSendWarning[SENSE_WARNING] == true)
      {
        SendWarning();

        g_iSendWarning[SENSE_WARNING] = false;
        g_ulWarStartTime_ms[SENSE_WARNING] = millis();
      }
    break;

    deafult:
      return;
  }

  // #ifdef PRINT_DEBUG
  //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "%s", g_arrcMsgTxt);
  //   Serial.println(g_arrcMsg);
  // #endif

  // for(int i = 0; i < MAX_WARNING_COUNT; i++)
  // {
  //   #ifdef PRINT_DEBUG
  //     snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "[%d] State: %d Start Time: %d sec", i, g_iSendWarning[i], (g_ulWarStartTime_ms[i] / 1000UL));
  //     Serial.println(g_arrcMsg);
  //   #endif
  // }
}

/***********************************************************************************************/
/*! 
* \fn         :: SendWarning()
* \author     :: Vignesh S
* \date       :: 23-Jun-2020
* \brief      :: This sends warnings call and warning messages
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void SendWarning()
{
#ifdef ENABLE_WARNING
  /* disable caller ID */
  EnableCallerId(false);

  /* send message */
  for(int i = 0; i < MAX_CONTACT_NUMBERS_STORED; i++)
  {
    /* call and hangup */
    MakeCall(ContactNumbers[i]);
    delay(CALL_TIMEOUT_SEC * 1000UL);

    HangupCall();
    
    /* send message */
    SendMessage(ContactNumbers[i], g_arrcMsgTxt);

    /* delay */
    delay(2000);
  }

  /* disable caller ID */
  EnableCallerId(true);
#endif
}

/***********************************************************************************************/
/*! 
* \fn         :: detectLowBatt()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function detects battery volagte level from BATT_MON_PIN
* \param[in]  :: None
* \param[out] :: None
* \return     :: true | false
*/
/***********************************************************************************************/
bool detectLowBatt()
{
  int iBattVolt = 0;
  bool LowBattState = false;

  /* average over LOW_BAT_MAX_ADC_SAMPLES */
  for(int  i = 0; i < LOW_BAT_MAX_ADC_SAMPLES; i++)
  {
    iBattVolt += analogRead(BATT_MON_PIN);
    delay(5);
  }

  iBattVolt /= LOW_BAT_MAX_ADC_SAMPLES;

  if(iBattVolt < LOW_BATT_THRESHOLD)
  {
    LowBattState = true;
  }

  // #ifdef PRINT_DEBUG
  //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Bat Voltage: %d", iBattVolt);
  //   Serial.println(g_arrcMsg);
  // #endif

  return LowBattState;
}

/***********************************************************************************************/
/*! 
* \fn         :: detectSensePin()
* \author     :: Vignesh S
* \date       :: 02-Jun-2020
* \brief      :: This function processes pulse generated by sense pin 
* \param[in]  :: iEventType (pulse count or level trigger)
* \param[out] :: None
* \return     :: true | false
*/
/***********************************************************************************************/
bool detectSensePin(int iEventType)
{
  bool SenseState = false;
  byte pinState = 0;
  unsigned long CurTime = (millis() / 1000);

  /* if system state is OFF then just return false */
  if(Rly.getState() == RELAY_OFF)
  {
    return false;
  }

  switch(iEventType)
  {
    case SENSE_EVENT_PULSE_COUNT:

      /* detect low */
      pinState = digitalRead(PULSE_SENSE_PIN);

      /* increment the pulse count if previous state is not equal to current state */
      if(g_PrePinState != pinState)
      {
        g_vulPulseCount++;
      }

      // #ifdef PRINT_DEBUG
      //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Prev: %d Pin: %d Pulse: %d", g_PrePinState, pinState, g_vulPulseCount);
      //   Serial.println(g_arrcMsg);
      // #endif

      /* monitor pulse count for every cycle */
      if((CurTime > 0) && ((CurTime % SENSE_MONITOR_PERIOD_SEC) == 0) && (CurTime != g_ulPreTime))
      {
        if(g_vulPulseCount < SENSE_PULSE_PER_PERIOD)
        {
          SenseState = true;
        }

        // #ifdef PRINT_DEBUG
        //   // snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "[%d sec] Pulse Count: %d", CurTime, g_vulPulseCount);
        //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Pulse Count: %d", g_vulPulseCount);
        //   Serial.println(g_arrcMsg);
        // #endif

        /* reset the count */
        g_vulPulseCount = 0;

      }

      /* assign current pin state previous state */
      g_PrePinState = pinState;
      g_ulPreTime = CurTime;

    break;

    case SENSE_EVENT_LOW_TO_HIGH:

      /* detect low */
      pinState = digitalRead(PULSE_SENSE_PIN);

      // #ifdef PRINT_DEBUG
      //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Prev: %d Pin: %d", g_PrePinState, pinState);
      //   Serial.println(g_arrcMsg);
      // #endif

      if((g_PrePinState == LOW) && (pinState == HIGH))
      {
        SenseState = true;
      }
      
      /* assign current pin state previous state */
      g_PrePinState = pinState;

    break;

    case SENSE_EVENT_HIGH_TO_LOW:

      /* detect HIGH */
      pinState = digitalRead(PULSE_SENSE_PIN);

      // #ifdef PRINT_DEBUG
      //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Prev: %d Pin: %d", g_PrePinState, pinState);
      //   Serial.println(g_arrcMsg);
      // #endif

      if((g_PrePinState == HIGH) && (pinState == LOW))
      {
        SenseState = true;
      }
      
      /* assign current pin state previous state */
      g_PrePinState = pinState;

    break;

    default:
    ;
  }

  return SenseState;
}


/***********************************************************************************************/
/*! 
* \fn         :: MakeCall()
* \author     :: Vignesh S
* \date       :: 06-Jun-2020
* \brief      :: This function makes a call to PhNumber
* \param[in]  :: PhNumber - Phone number (10 digit string)
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void MakeCall(const char *PhNumber)
{
  snprintf(g_arrcGSMMsg, MAX_CMD_STRING_SIZE, "ATD+91%s;", PhNumber);
  SS_GSM.println(g_arrcGSMMsg);
  #ifdef PRINT_DEBUG
    snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Calling cmd: %s\n", g_arrcGSMMsg);
    Serial.println(g_arrcMsg);
  #endif
}

/***********************************************************************************************/
/*! 
* \fn         :: HangupCall()
* \author     :: Vignesh S
* \date       :: 06-Jun-2020
* \brief      :: This function hangups a call
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void HangupCall()
{
  SS_GSM.println("ATH");
  #ifdef PRINT_DEBUG
    snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Hangup Call\n");
    Serial.println(g_arrcMsg);
  #endif
  delay(1000);
}

/***********************************************************************************************/
/*! 
* \fn         :: ReceiveCall()
* \author     :: Vignesh S
* \date       :: 06-Jun-2020
* \brief      :: This function sets the GSM module to receive a call 
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void ReceiveCall()
{
  SS_GSM.println("ATA");
  #ifdef PRINT_DEBUG
    snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Receiving Call\n");
    Serial.println(g_arrcMsg);
  #endif
}

/***********************************************************************************************/
/*! 
* \fn         :: EnableCallerId()
* \author     :: Vignesh S
* \date       :: 18-Jun-2020
* \brief      :: This function enables or disables caller iD
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void EnableCallerId(bool state)
{
  if(state == true)
  {
    SS_GSM.println("AT+CLIP=1\r");
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Caller ID enabled");
      Serial.println(g_arrcMsg);
    #endif
  }
  else
  {
    SS_GSM.println("AT+CLIP=1\r");
    #ifdef PRINT_DEBUG
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Caller ID disabled");
      Serial.println(g_arrcMsg);
    #endif
  }

  /* delay for mode switch */
  delay(2000);
}

/***********************************************************************************************/
/*! 
* \fn         :: SendMessage()
* \author     :: Vignesh S
* \date       :: 06-Jun-2020
* \brief      :: This function sends a message
* \param[in]  :: PhNumber - Phone number (10 digit string)
* \param[in]  :: Message - Message to send
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void SendMessage(const char *PhNumber, const char *Message)
{
  SS_GSM.println("AT+CMGF=1");    //Sets the GSM Module in Text Mode
  delay(1000);  // Delay of 1000 milli seconds or 1 second
  
  snprintf(g_arrcGSMMsg, MAX_CMD_STRING_SIZE, "AT+CMGS=\"+91%s\"\r", PhNumber);
  SS_GSM.println(g_arrcGSMMsg); 
  delay(1000);

  #ifdef PRINT_DEBUG
    snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "Sending: %s to %s", Message, PhNumber);
    Serial.println(g_arrcMsg);
  #endif

  SS_GSM.println(Message);// The SMS text you want to send
  delay(100);
  SS_GSM.println((char)26);// ASCII code of CTRL+Z
  delay(1000);
}

/***********************************************************************************************/
/*! 
* \fn         :: printBytes()
* \author     :: Vignesh S
* \date       :: 06-Jun-2020
* \brief      :: This function each character in hexadecimal form
* \param[in]  :: array
* \param[in]  :: Size
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void printBytes(char *array, int iSize)
{
  for(int i = 0; i < iSize; i++)
  {
    #ifdef PRINT_DEBUG
      memset(g_arrcMsg, 0, sizeof(g_arrcMsg));
      snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "[%d]: 0x%x[%c]", i, array[i], array[i]);
      Serial.println(g_arrcMsg);
    #endif
  }
}

/***********************************************************************************************/
/*! 
* \fn         :: GSM_PowerUpDown()
* \author     :: Vignesh S
* \date       :: 22-Jun-2020
* \brief      :: This function toggles the GSM_POWER_KEY
* \param[in]  :: none
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
void GSM_PowerUpDown()
{
  delay(GSM_POWER_KEY_PULSE_TIME_MS / 2);
  digitalWrite(GSM_POWER_KEY, HIGH);
  delay(GSM_POWER_KEY_PULSE_TIME_MS);
  digitalWrite(GSM_POWER_KEY, LOW);  
  delay(GSM_POWER_KEY_PULSE_TIME_MS / 2);
}

/***********************************************************************************************/
/*! 
* \fn         :: detectGSMPowerDown()
* \author     :: Vignesh S
* \date       :: 22-Jun-2020
* \brief      :: This function detectes power down 
* \param[in]  :: none
* \param[in]  :: None
* \param[out] :: None
* \return     :: None
*/
/***********************************************************************************************/
bool detectGSMPowerDown(char *string, int iSize)
{
  char testStr[] = GSM_POWER_DOWN_STR;
  
  // #ifdef PRINT_DEBUG
  //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "CMP: %s CMP Size:%d", string, iSize);
  //   Serial.println(g_arrcMsg);
  // #endif
  
  if(iSize < strlen(testStr))
  {
    return false;    
  }

  for(int i = 0; i < (iSize - strlen(testStr)); i++)
  {
    // #ifdef PRINT_DEBUG
    //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "[%d] CMP: %c and %c", i, string[i], testStr[0]);
    //   Serial.println(g_arrcMsg);
    // #endif

    if(string[i] == testStr[0])
    {
      if(StrnCmp(&string[i], testStr, strlen(testStr)))
      {
        // #ifdef PRINT_DEBUG
        //   snprintf(g_arrcMsg, MAX_DEBUG_MSG_SIZE, "[%d] CMP: %s matches %s", i, &string[i], testStr);
        //   Serial.println(g_arrcMsg);
        // #endif

        return true;
      }
    }
  }

  return false;
}
