# Temporary RAM calibration

Baseline pressure coefficients: ED09 E9EE 004F 0000 0033 00BE 09DC FA9A.
EEPROM signature: 46C8. ADC resolution: 13 bits.
Measured means: 2479.5 counts at 0 bar absolute, 29542.625 at 1 bar absolute.
Targets: 0.500 V and 4.500 V at 5 V sensor supply.

Using Functional Description section 2.2, invert the original nonlinearity
polynomial at both measured and target endpoints. Transform its input as
Ynew=a*Yold+b, with a=0.968585784978 and b=0.026762464035.
For numerator N and denominator D, use Nnew=N+(b/a)*D and Dnew=D/a.
This changes c0,c4,c5 by (b/a) times c1,c6,c7 respectively, and divides
c1,c6,c7 by a. Keep c2,c3. Round to signed 16-bit coefficient values.
Candidates: EC6D E937 004F 0000 0079 0098 0A2E FA6D.

Numerical checks use normalized raw temperatures -0.5, 0 and +0.5, reconstruct
the raw pressure corresponding to each measured endpoint, and evaluate the
rounded candidate coefficients. Both endpoints remain within 2 mV of their
targets in these checks. Hardware verification remains required. Intermediate
pressure and temperature accuracy cannot be established from two endpoints.

Upload, power-cycle the SENSOR, wait at least 100 ms, then send c FIRST.
Command mode requires 72 as the first command after power-on. Firmware reads
EEPROM twice, checks the baseline, copies EEPROM into RAM, writes and reads
back all candidate coefficient words and the RAM signature, then starts the
RAM measurement cycle. The trial itself does not write EEPROM.

Send d at both endpoints and at an intermediate reference pressure. Send r
to restore EEPROM calibration. Sensor power-cycling also restores it, but
the Nano cannot detect that reset: send r to clear its trial status too.
Do not reboot only the Nano during a RAM trial.

Exactly 0.500 V is not a DAC step at 5 V (adjacent: 0.498 V and 0.5005 V).
The model predicts code 204 (0.498 V) at 0 bar, i.e. just below a strict
0.5 V cutoff; noise and supply variation add to this. Accepted as is.
Set the firmware voltage constants to measured supply values.

## Save the verified trial

After uploading the firmware, power-cycle the sensor and run c again first:
rebooting the Nano clears its trial tracking. Verify d at both endpoints, then
send s to persist the active trial. It compares all 32 RAM words to an independent
trial snapshot and checks the signature before sending COPY_RAM2EEP (C3).
Keep power connected during the one-second programming wait. It reads every
EEPROM word back and checks both the signature and full image before reporting
SAVE VERIFIED and resuming measurement from EEPROM.

After saving, r reloads the newly saved calibration; it cannot undo the save.
Keep the original backup externally. The c baseline guard intentionally rejects
EEPROM that has already been changed. If save verification fails, EEPROM state
is uncertain; the firmware attempts to resume the trial from RAM and asks you
not to power-cycle until that failure is investigated.
