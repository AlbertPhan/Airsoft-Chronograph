/*

Project: Airsoft Chronograph
Author: Albert Phan
Date started: March 20, 2015
Last Update: Dec 13, 2015

Pinouts

LCD
A4 SDA
A5 SCL

D5 OK Button
D4 Down Button
D3 Up Button
D2 Test Button

Comparator
AIN0 D6 Positive pin IR receivers outputs
AIN1 D7 Negative pin (reference pin) ~0.3-1V

*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>

#define IRSPACING 0.080 //80 mm
#define CLKPERIOD 0.0000000625  //62.5 nS 16MHz external Oscillator
#define T1OVFPERIOD 0.004096		// 4.096 ms overflow timer1 period
#define TIMEOUTTIME (0.020/T1OVFPERIOD)	//20 ms/1 overflow time 4.096 mS
#define T2OVFPERIOD 0.000016			// 16 uS Timer2 overflow period		
#define ROFTIMEOUTTIME (0.5/T2OVFPERIOD) // timeout time in seconds/ OVF period
#define BBMINTIME 10			// in us
#define FPSCONVERSIONFACTOR 3.28084	// meters per second to feet per second
#define BBWEIGHTTOKG 100000	// bbWeight conversion to KG

#define OK_BTN 5
#define DOWN_BTN 4
#define UP_BTN 3
#define TEST_BTN 2
#define LED_PIN 13

#define DEBOUNCE_TIME 10 // How many ms must pass before the button is considered stable
//#define REPEAT_INTERVAL 50 // How many ms in between button presses while holding it down



#define	NORMALIZED 4
#define JOULES 3
#define	AVERAGING 2
#define MINMAX 1
#define	RATEOFFIRE 0
#define MAXMENUS 4

Bounce okBtn(OK_BTN,DEBOUNCE_TIME);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME);
Bounce testBtn(TEST_BTN, DEBOUNCE_TIME);

LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

unsigned char bbPresent = 0;
unsigned char dataReady = 0;
unsigned char rofReady = 1;
unsigned char updateFlag = 0;
unsigned char editingFlag = 0;
unsigned char bbDecrementFlag = 0;
unsigned char bbIncrementFlag = 0;
unsigned char menuIncrementFlag = 0;
unsigned char menuDecrementFlag = 0;
unsigned char ledState = 0;

unsigned long int timer1Overflows = 0;
unsigned long int timer2Overflows = 0;
unsigned long int previous_millis = 0;
unsigned long int previous_micros = 0;
unsigned long int previous_rofmicros = 0;

unsigned int bbCount = 0;
unsigned int prevbbCount = 0;
double fps = 338.06;	// initially 338.06 for testing because it's 400 fps normalized
double normalizedfps = 0;
double minfps = 10000;	// default values
double maxfps = 0;
double bps = 0;	// BBs per second

//double averagefps = 0;
double averageSum = 0;

unsigned char bbWeight = 28;	//initially 0.28g as it is the most common
unsigned char menuState = NORMALIZED;	//starts in normalized menu

// Function Protoypes
void drawScreen(); // Handles drawing screen for various menus
void fpsReady();	// Updates dataReady, bbcount, averagesum, min and max.


ISR (ANALOG_COMP_vect) // When bb passes or leaves an IR beam (or a noise spike).
{
	if(ACSR & bit(ACO))	//Comp output rose
	{
		previous_micros = micros();
		digitalWrite(LED_PIN, HIGH);	//debugging led
	}
	else // Comp output fell
	{
		digitalWrite(LED_PIN, LOW);		//debugging led
		if((micros() - previous_micros ) > BBMINTIME) // Comp output pulse is a bb and not a noise spike
		{
			if(bbPresent)  //if bb hit second beam
			{
				//stop timer1
				TCCR1B = 0;
				bbPresent = 0;
				fps = IRSPACING/((TCNT1 + timer1Overflows * 65536) * CLKPERIOD) * FPSCONVERSIONFACTOR;
				
				// discard any ridiculous values
				if(fps <= 5000)
				{
					fpsReady();
				}
				else
				{
					dataReady = 0;
				}
				updateFlag = 1;
			}
			else  //bb hit first beam
			{
				//start timer1
				TCNT1 = 0; //clear timer
				timer1Overflows = 0; //clear overflows
				TCCR1B = bit(CS10);  // No prescaling 16 Mhz = 62.5 nS period
				bbPresent = 1;
			}
		}
	}
	
}


ISR (TIMER1_OVF_vect)  //handles timer1 overflows
{
	if(timer1Overflows > TIMEOUTTIME)	//if bb timeouts (doesnt hit second beam)
	{
		//stop timer
		TCCR1B = 0;
		bbPresent = 0;
		dataReady = 0;
		updateFlag = 1;
	}
	else
	{
		timer1Overflows++;
	}
	
}
ISR (TIMER2_OVF_vect)  //handles timer2 overflows
{
	if(timer2Overflows > ROFTIMEOUTTIME)	// X amount of time after last bb shot measured
	{
		rofReady = 1; // Burst over, ready to start the timer of next burst
		TCCR2B = 0;	//stop timer
		
		// BBs per second calculation
		if(bbCount - prevbbCount > 2)	// Only do calculation if multiple bbs were fired
		{
			// bps = amount of bbs during burst/ time during burst
			bps = (bbCount - prevbbCount)/((micros() - previous_rofmicros)/1000000.0 - ROFTIMEOUTTIME*T2OVFPERIOD);
		}
		
		
		prevbbCount = bbCount;

		if(menuState == RATEOFFIRE)	// Update immediately if in rateoffire screen otherwise no need to update
		{
			updateFlag = 1;
		}
		
	}
	else
	{
		timer2Overflows++;


	}
	
}

void setup()
{
	//Analog Comparator setup
	ADCSRB = 0;           // (Disable) ACME: Analog Comparator Multiplexer Enable
	ACSR =  bit(ACI)     // (Clear) Analog Comparator Interrupt Flag
	| bit(ACIE)    // Analog Comparator Interrupt Enable
	
	& ~bit(ACIS1)  // ACIS1, ACIS0: Analog Comparator Interrupt Mode Select (trigger on toggle)
	& ~bit(ACIS0);                                             
	
	ADMUX |= bit(REFS1) 
	|bit(REFS0);
	
	//analogReference(INTERNAL); // Set AREF to internal 1.1V for use with comparator
	
	// Pin Setups
	pinMode(LED_PIN, OUTPUT); // Debugging led
	pinMode(OK_BTN, INPUT);
	pinMode(DOWN_BTN, INPUT);
	pinMode(UP_BTN, INPUT);
	pinMode(TEST_BTN, INPUT);
	
	// timer0 used for delay functions in lcd library do not use timer0
	
	//16 bit Timer1 setup (used for bb fps calculations)
	TCCR1A = 0;  //normal mode counts up to 0xFFFF and restarts
	TCCR1B = 0;	 //timer stopped
	TIMSK1 |= bit(TOIE1);   // enable timer overflow interrupt
	
	//8 bit Timer2 setup (used for Rate of fire timeout)
	TCCR2A = 0; //normal mode
	TCCR2B = 0; //timer stopped
	TIMSK2 |= bit(TOIE2);	// enable tof interrupt
	
	// Serial stuff for debugging
	Serial.begin(115200);
	Serial.println("Started.");
	
	lcd.init();                      // initialize the lcd
	
	// Print intro to the LCD.
	lcd.backlight();
	lcd.clear();
	lcd.print("Airsoft");
	lcd.setCursor(0,1);
	lcd.print("Chronograph");
	//delay(2000);
	
	lcd.clear();
	lcd.print("Version 1.0");
	lcd.setCursor(0,1);
	lcd.print("by Albert Phan");
	//delay(3000);
	
	lcd.clear();
	lcd.print("www.pegasus");
	lcd.setCursor(0,1);
	lcd.print("prototyping.ca");
	delay(1000);
	lcd.clear();
	
	// Displays test fps
	dataReady = 1;
	updateFlag = 1 ;
}

void loop()
{
	//Update states of all buttons
	okBtn.update();
	upBtn.update();
	downBtn.update();
	testBtn.update();

	/////////////////////////////////////////////////////////////////////////
	// Button code for menu changes
	
	// if not editing a menu then up and down buttons change menu
	if(!editingFlag)
	{
		if(upBtn.rose())
		{
			menuIncrementFlag = 1;
		}
		else if(downBtn.rose())
		{
			menuDecrementFlag = 1;
		}
	}
	// If ok button is pressed in any menu except NORMALIZED and JOULES then clear data
	if(okBtn.rose() && !(menuState == NORMALIZED || menuState == JOULES))
	{
		fps = 0;
		bbCount = 0;
		prevbbCount = 0;
		rofReady = 1;
		averageSum = 0;
		bps = 0;
		minfps = 10000;
		maxfps = 0;
		updateFlag = 1;
	
	}
	
	if(menuIncrementFlag)
	{
		
		if(menuState == MAXMENUS)
		{
			menuState = 0;
		}
		else 
		{
			menuState++;
		}
		menuIncrementFlag = 0;
		updateFlag = 1;
		lcd.clear();
	}
	else if(menuDecrementFlag)
	{
		
		if(menuState == 0)
		{
			menuState = MAXMENUS;
		}
		else
		{
			menuState--;
		}
		menuDecrementFlag = 0;
		updateFlag = 1;
		lcd.clear();
	}
	
	//Testing button, used for generating fake fps numbers
	if(testBtn.rose())
	{
		fps = (double)((rand() % 500 + 1) + ((rand() % 101 + 1)/100.0));
		fpsReady();
	}
	if(testBtn.retrigger())
	{
		fps = (double)((rand() % 500 + 1) + ((rand() % 101 + 1)/100.0));
		fpsReady();
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Normalized and JOULES Menu specific code
	if(menuState == NORMALIZED || menuState == JOULES)
	{
		//button code for normalized menu
		if(okBtn.rose())	//ok button pressed?
		{
			if(editingFlag) //if already editing, stop editing
			{
				editingFlag = 0;
			}
			else
			{
				editingFlag = 1;
			}
		}
		// if up or down was pressed while editing
		if(editingFlag)
		{
			if(upBtn.rose())
			{
				bbIncrementFlag = 1;
			}
			else if(downBtn.rose())
			{
				bbDecrementFlag = 1;
			}
			
			// if up or down button held down while editing, repeat button presses at REPEAT_INTERVAL
			if(upBtn.retrigger())
			{
				bbIncrementFlag = 1;
			}
			else if(downBtn.retrigger())
			{
				bbDecrementFlag = 1;
			}
		}
		
		// Deals with changing bb weight
		if(bbIncrementFlag)
		{
			bbWeight++;
			if(bbWeight > 99)
			{
				bbWeight = 0;
			}
			bbIncrementFlag = 0;
			updateFlag = 1;
		}
		else if(bbDecrementFlag)
		{
			bbWeight--;
			
			if(bbWeight > 99)
			{
				bbWeight = 99;
			}
			bbDecrementFlag = 0;
			updateFlag = 1;
		}
		
		if(updateFlag) //if anything changed
		{
			// Printing top row: "FPS|0.28g:XXX.XX"
			//		bottom  row: "FPS|0.20g:XXX.XX"
			//	OR other bottom: "Joules: X.XX     "
			//	column:			  01234567890123456
			drawScreen();
		}
		
		if(editingFlag)
		{
			lcd.blink();		// keeps blinking while editing
			lcd.setCursor(7,0);
		}
		else
		{
			lcd.noBlink();
		}
		

	}
	else if(menuState == AVERAGING)
	{
		if(updateFlag)
		{
			
			// Printing top row "AVG:XXX.XX BB:   "
			//		 bottom row "MAX Diff:XXX.XX"
			//					 01234567890123456	
			drawScreen();
		}
		
		
	}
	else if (menuState == MINMAX)
	{
		// Printing top row "MIN:xxx.xx"
		//					"MAX:xxx.xx"
		//					 01234567890123456
		if (updateFlag)
		{
			drawScreen();
		}
	}
	else if (menuState == RATEOFFIRE)
	{
		if (updateFlag)
		{
			drawScreen();
		}
	}

}

void fpsReady()
{
	dataReady = 1;
	bbCount++;
	averageSum += fps;
	
	//start timer2 for ROF timeout
	TCNT2 = 0; // resets timer for the timeout
	timer2Overflows = 0; // clear overflows
	TCCR2B = bit(CS20);  // No prescaling 16 Mhz = 62.5 nS period
	
	if(rofReady)	// Start of bb ROF burst
	{
		previous_rofmicros = micros(); // time at start of ROF burst
		rofReady = 0;	
	}
	
	
	if(fps < minfps)
	{
		minfps = fps;
	}
	if(fps > maxfps)
	{
		maxfps = fps;
	}
	updateFlag = 1;
}

void drawScreen()
{
	if(menuState == NORMALIZED)
	{
		// Printing top row: "FPS|0.28g:XXX.XX"
		lcd.home();
		lcd.print("FPS|0.");
		// If bbWeight is a single digit, add leading zero
		if(bbWeight < 10)
		{
			lcd.print("0");
		}
		lcd.print(bbWeight);
		lcd.print("g:");
		lcd.print("      "); //clears the fps space
		lcd.setCursor(10,0);
		if(dataReady) //if no error
		{
			lcd.print(fps);
		}
		else
		{
			lcd.print("XXX.XX");
		}
		
		// Printing bottom row: "FPS|0.20g:XXX.XX"
		lcd.setCursor(0,1);
		lcd.print("FPS|0.20g:");
		lcd.print("      "); //clears the fps space
		lcd.setCursor(10,1);
		if(dataReady) //if fps data is good and ready
		{
			//Energy (in joules) = 1/2 mass * velocity^2
			//therefore normalized Velocity (0.20g) = sqrt((0.5*bbweight*fps^2)*2/0.20)
			lcd.print(sqrt((0.5 * bbWeight * fps * fps)* 2 / 20));
		}
		else
		{
			lcd.print("XXX.XX");
		}
		// done updating
	}
	else if(menuState == JOULES)
	{
		// Printing top row: "FPS|0.28g:XXX.XX"
		lcd.home();
		lcd.print("FPS|0.");
		// If bbWeight is a single digit, add leading zero
		if(bbWeight < 10)
		{
			lcd.print("0");
		}
		lcd.print(bbWeight);
		lcd.print("g:");
		lcd.print("      "); //clears the fps space
		lcd.setCursor(10,0);
		if(dataReady) //if no error
		{
			lcd.print(fps);
		}
		else
		{
			lcd.print("XXX.XX");
		}
		
		// Printing bottom row:"Joules: X.XX     
		//					    01234567890123456
		lcd.setCursor(0,1);
		lcd.print("Joules:   ");
		if(dataReady) //Data ready? display joules, else display X.XX
		{
			// Energy (in joules) = 1/2 mass * velocity^2
			lcd.print(0.5 * bbWeight/BBWEIGHTTOKG * sq(fps/FPSCONVERSIONFACTOR));
		}
		else
		{
			lcd.print("X.XX");
		}
		lcd.print("     ");	// Clear rest of row 2
		
	}
	else if(menuState == AVERAGING)
	{
		// Printing top row "AVG:XXX.XX BB:   "
		//					 01234567890123456
		lcd.home();
		lcd.print("AVG:");
		// if bbCount is cleared, just display 0.0 for avg fps
		lcd.print(bbCount == 0? 0.0 : averageSum / bbCount);
		lcd.print(" BB:");
		lcd.print(bbCount > 99?  99: bbCount);	// If bbcount is > 99 only display 99
		lcd.print("   "); // Clear rest of Row 1
		
		// Printing Bottom row "Max Diff:XXX.XX  "
		//                      01234567890123456
		lcd.setCursor(0,1);
		lcd.print("MAX Diff:");
		// If minfps represents real data then display it
		lcd.print(minfps == 10000? 0.0: maxfps-minfps); // For debugging
		lcd.print("     "); // Clear rest of row 2
	}
	else if(menuState == MINMAX)
	{
		// Printing top row "MIN:xxx.xx J:X.XX
		//					 01234567890123456
		// Printing bot row "MAX:xxx.xx    
		lcd.home();
		lcd.print("MIN:");
		// If minfps represents real data then display it
		lcd.print(minfps == 10000? 0.0:  minfps);
		lcd.print("   ");	//clear rest of row 1
		lcd.setCursor(0,1);
		lcd.print("MAX:");
		lcd.print(maxfps);
		lcd.print("   ");	//clear rest of row 2
		
	}
	else if (menuState == RATEOFFIRE)
	{
		
		lcd.home();
		lcd.print("BPS:");
		lcd.print(bps);
		lcd.print("          "); //clear rest of row 1
		lcd.setCursor(0,1);
		lcd.print(ADMUX,2);	// debugging
	}
	updateFlag = 0;
}





