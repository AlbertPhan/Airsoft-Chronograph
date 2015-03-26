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
AIN0 D6 Positive pin
AIN1 D7 Negative pin (reference pin) ~0.3-1V

*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h> //uses modified bounce2 library

#define IRSPACING 0.080 //80 mm
#define CLKPERIOD 0.0000000625  //62.5 nS 16MHz external Oscillator
#define TIMEOUTTIME (50/4.096)	//50 ms/1 overflow time 4.096 mS

#define OK_BTN 5
#define DOWN_BTN 4
#define UP_BTN 3
#define TEST_BTN 2

#define DEBOUNCE_TIME 5 // How many ms must pass before the button is considered stable
#define REPEAT_INTERVAL 50 // How many ms inbetween button presses while holding it down


#define	NORMALIZED 0
#define	RATEOFFIRE 1
#define	AVERAGING 2


Bounce okBtn(OK_BTN,DEBOUNCE_TIME);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME);
Bounce testBtn(TEST_BTN,DEBOUNCE_TIME);




LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

unsigned char bbPresent = 0;
unsigned char dataReady = 0;
unsigned char updateFlag = 0;
unsigned char editingFlag = 0;
unsigned char bbDecrementFlag = 0;
unsigned char bbIncrementFlag = 0;
unsigned char menuIncrementFlag = 0;
unsigned char menuDecrementFlag = 0;

unsigned long int timerOverflows = 0;
unsigned long int previous_millis = 0;

unsigned int bbAvgcount = 0;

double fps = 338.06;	//initially 338.06 for testing
double normalizedfps = 0;
double averageSum = 0;

unsigned char bbWeight = 28;	//initially 0.28g as it is the most common
unsigned char menuState = NORMALIZED;	//starts in normalized menu


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
			dataReady = 1;
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
	
	
	//16 bit Timer1 setup (used for bb fps calculations)
	TCCR1A = 0;  //normal mode counts up to 0xFFFF and restarts
	TCCR1B = 0;	 //timer stopped
	TIMSK1 |= bit(TOIE1);   // enable timer overflow interrupt
	
	// timer0 used for delay functions in lcd library do not use timer0
	
	//Serial.begin (115200);
	//Serial.println ("Started.");
	
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

	///////////////////////////////////////////////////////////////
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
	
	if(menuIncrementFlag)
	{
		menuState++;
		if(menuState > 2)
		{
			menuState = 0;
		}
		menuIncrementFlag = 0;
		updateFlag = 1;
		lcd.clear();
	}
	else if(menuDecrementFlag)
	{
		menuState--;
		if(menuState > 2)
		{
			menuState = 2;
		}
		menuDecrementFlag = 0;
		updateFlag = 1;
		lcd.clear();
	}
	
	// Testing button, used for generating fake fps numbers
	if(testBtn.rose())
	{
		fps = (double)((rand() % 500 + 1) + ((rand() % 101 + 1)/100.0));
		dataReady = 1;
		updateFlag = 1;
	}	
	////////////////////////////////////////////////
	// Menu specific code
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
		if((upBtn.rose() || downBtn.rose()) && editingFlag)
		{
			if(upBtn.rose())
			{
				bbIncrementFlag = 1;
			}
			else if(downBtn.rose())
			{
				bbDecrementFlag = 1;
			}
		}
		// if up or down button held down, repeat button presses at REPEAT_INTERVAL
		if((upBtn.held() || downBtn.held()) && editingFlag)
		{
			
			if(previous_millis !=0)
			{
				if (millis() - previous_millis >= REPEAT_INTERVAL)
				{
					if(upBtn.held())
					{
						bbIncrementFlag = 1;	//increment button
					}
					else if(downBtn.held())
					{
						bbDecrementFlag = 1;	//decrement button
					}
					previous_millis = 0;
				}
				
			}
			else
			{
				previous_millis = millis();	
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
		
		/////////////////////////////////////////////////////////////
		// Normalized FPS menu LCD stuff
		if(updateFlag) //if anything changed
		{
			//Energy (in joules) = 1/2 mass * velocity^2
			//therefore normalized Velocity (0.20g) = sqrt((0.5*bbweight*fps^2)*2/0.20)
			normalizedfps = sqrt((0.5 * bbWeight * fps * fps)* 2 / 20);
			
			// Printing top row: "FPS|0.28g:XXX.XX"
			//	column:			  01234567890123456
			
			lcd.home();
			lcd.print("FPS|0.");
			if(bbWeight < 10) //if single digit bb weight then add leading zero
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
			if(dataReady) //if no error
			{
				lcd.print(normalizedfps);
			}
			else
			{
				lcd.print("XXX.XX");
			}
			// done updating
			updateFlag = 0;
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
		

	}//end Normalized menu
	else if(menuState == AVERAGING)
	{
		if(updateFlag)
		{
			
			
			lcd.home();
			lcd.print("");
			
		}
		
		
	}
	else if (menuState == RATEOFFIRE)
	{
		lcd.home();
		lcd.print("Rate of fire menu");
	}

}






