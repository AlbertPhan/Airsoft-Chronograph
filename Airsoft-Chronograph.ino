/*

Project: Airsoft Chronograph
Author: Albert Phan
Date started: March 20, 2015

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
#define TIMEOUTTIME (20/4.096)	//20 ms/1 overflow time 4.096 mS

#define OK_BTN 5
#define DOWN_BTN 4
#define UP_BTN 3
#define TEST_BTN 2
#define LED_PIN 13

#define DEBOUNCE_TIME 10 // How many ms must pass before the button is considered stable
//#define REPEAT_INTERVAL 50 // How many ms in between button presses while holding it down



#define	NORMALIZED 3
#define	AVERAGING 2
#define MINMAX 1
#define	RATEOFFIRE 0
#define MAXMENUS 3

Bounce okBtn(OK_BTN,DEBOUNCE_TIME);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME);
Bounce testBtn(TEST_BTN, DEBOUNCE_TIME);

LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

unsigned char bbPresent = 0;
unsigned char dataReady = 0;
unsigned char updateFlag = 0;
unsigned char editingFlag = 0;
unsigned char bbDecrementFlag = 0;
unsigned char bbIncrementFlag = 0;
unsigned char menuIncrementFlag = 0;
unsigned char menuDecrementFlag = 0;
unsigned char ledState = 0;

unsigned long int timerOverflows = 0;
unsigned long int previous_millis = 0;

unsigned int bbCount = 0;

double fps = 338.06;	//initially 338.06 for testing
double normalizedfps = 0;
double minfps = 10000;
double maxfps = 0;
//double averagefps = 0;
double averageSum = 0;

unsigned char bbWeight = 28;	//initially 0.28g as it is the most common
unsigned char menuState = NORMALIZED;	//starts in normalized menu

// Function Protoype
void drawScreen(); // Handles drawing screen for various menus
void fpsReady();	// Updates dataReady, bbcount, averagesum, min and max.


ISR (ANALOG_COMP_vect) //When bb passes a beam.
{
	if(bbPresent)  //if bb hit second beam
	{
		//stop timer1
		TCCR1B = 0;
		bbPresent = 0;
		fps = IRSPACING/((TCNT1 + timerOverflows * 65536) * CLKPERIOD) * 3.281;
		
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
		timerOverflows = 0; //clear overflows
		TCCR1B = bit(CS10);  // No prescaling 16 Mhz = 62.5 nS period
		bbPresent = 1;
	}
}


ISR (TIMER1_OVF_vect)  //handles timer1 overflows
{
	if(timerOverflows > TIMEOUTTIME)//if bb timeouts (doesnt hit second beam)
	{
		//stop timer
		TCCR1B = 0;
		bbPresent = 0;
		dataReady = 0;
		updateFlag = 1;
	}
	else
	{
		timerOverflows++;
	}
	
}

void setup()
{
	//Analog Comparator setup
	ADCSRB = 0;           // (Disable) ACME: Analog Comparator Multiplexer Enable
	ACSR =  bit(ACI)     // (Clear) Analog Comparator Interrupt Flag
	| bit(ACIE)    // Analog Comparator Interrupt Enable
	| bit(ACIS1)  // ACIS1, ACIS0: Analog Comparator Interrupt Mode Select (trigger on rising edge)
	| bit(ACIS0);
	
	
	// Pin Setups
	pinMode(LED_PIN, OUTPUT); // Debugging led
	pinMode(OK_BTN, INPUT);
	pinMode(DOWN_BTN, INPUT);
	pinMode(UP_BTN, INPUT);
	pinMode(TEST_BTN, INPUT);
	
	
	//16 bit Timer1 setup (used for bb fps calculations)
	TCCR1A = 0;  //normal mode counts up to 0xFFFF and restarts
	TCCR1B = 0;	 //timer stopped
	TIMSK1 |= bit(TOIE1);   // enable timer overflow interrupt
	
	// timer0 used for delay functions in lcd library do not use timer0
	
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
	// If ok button is pressed in any menu except NORMALIZED then clear data
	if(menuState != NORMALIZED && okBtn.rose())
	{
		fps = 0;
		bbCount = 0;
		averageSum = 0;
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
	// Normalized Menu specific code
	if(menuState == NORMALIZED)
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
	
	// Figures out smallest and larget fps
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
	else if(menuState == AVERAGING)
	{
		// Printing top row "AVG:XXX.XX BB:   "
		//					 01234567890123456
		lcd.home();
		lcd.print("AVG:");
		// if bbCount is cleared, just display 0.0 for avg fps
		lcd.print(bbCount == 0? 0.0 : averageSum / bbCount);
		lcd.print(" BB:");
		lcd.print(bbCount);	//doing bbcounts for now instead of max diff
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
		// Printing top row "MIN:xxx.xx       "
		//					 01234567890123456
		// Printing bot row "MAX:xxx.xx       "
		lcd.home();
		lcd.print("MIN:");
		// If minfps represents real data then display it
		lcd.print(minfps == 10000? 0.0 :  minfps);
		lcd.print("   "); //clear rest of row 1
		lcd.setCursor(0,1);
		lcd.print("MAX:");
		lcd.print(maxfps);
		lcd.print("   "); //clear rest of row 1
		
	}
	else if (menuState == RATEOFFIRE)
	{
		
		lcd.home();
		lcd.print("RATEOFFIRE MENU");
	}
	updateFlag = 0;
}





