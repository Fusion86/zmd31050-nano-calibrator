"""Generate an editable Excel model of Functional Description section 2.2."""
from pathlib import Path
import xlsxwriter

ROOT = Path(__file__).resolve().parent
old = [-4855, -5650, 79, 0, 51, 190, 2524, -1382]
new = [-5011, -5833, 79, 0, 121, 152, 2606, -1427]
a, b = 0.968585784977964, 0.026762464034538802

def f(y):
    return y * (1 - 79 / 32768) + 79 / 32768 * y * y

def inverse(p):
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if f(mid) < p:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2

path = ROOT / "calibration-comparison.xlsx"
with xlsxwriter.Workbook(path) as wb:
    header = wb.add_format({"bold": True, "bg_color": "#DBEAFE"})
    editable = wb.add_format({"bg_color": "#FEF3C7", "num_format": "0.000000"})
    number = wb.add_format({"num_format": "0.000000"})
    notes = wb.add_worksheet("Read me")
    notes.set_column("A:A", 120)
    lines = [
        "ZMD31050 temporary RAM calibration: editable Excel model",
        "Source: docs/ZMD31050_Functional_Description_Rev_0.82.pdf, Functional Description Rev. 0.82, section 2.2, printed page 7.",
        "Coefficients are signed 16-bit two's-complement values; hex columns show the transmitted words.",
        "Datasheet: t=Z_T1/2^(rADC-1); Y=(Z_p+c0+c4*t+c5*t^2)/(c1+c6*t+c7*t^2).",
        "Datasheet: P=Y*(1-c2/32768-c3/32768)+c2*Y^2/32768+c3*Y^3/32768; ideal count=P*32768.",
        "Z_p and Z_T1 are RAW auto-zero-corrected ADC values, not the calibrated digital count returned by d.",
        "Raw ADC defaults on Function sheet are reconstructed for the pumped mean with t=0; they are not measured raw ADC values.",
        "Endpoints: pumped mean=2479.5 counts at 0 bar absolute; vented mean=29542.625 at 1 bar absolute.",
        "Targets: normalized P=0.1 and 0.9, corresponding to 0.500 and 4.500 V at 5 V VDDA.",
        "Invert the original polynomial at measured/target endpoints, then fit Ynew=a*Yold+b.",
        "With N and D denoting original numerator/denominator: Nnew=N+(b/a)*D, Dnew=D/a.",
        "Therefore c0,c4,c5 gain (b/a) times c1,c6,c7, respectively; c1,c6,c7 divide by a. Keep c2,c3.",
        "Curve sheet interpolates original calibrated counts vs pressure, reconstructs raw pressure, then evaluates candidate coefficients.",
        "Curve is an endpoint-based prediction, not measured intermediate-pressure calibration. Temperature behavior is not verified.",
        "DAC voltage uses floor(ideal count/16), bounded by 0..2047. Internal ASIC rounding/precision may differ from Excel.",
        "Exactly 0.500 V cannot be represented by the 11-bit DAC at 5 V: adjacent outputs are 0.498046875 and 0.50048828125 V.",
        "RAM test c is temporary. s permanently saves the verified trial to EEPROM. After saving, r/power-cycle loads the newly saved calibration.",
    ]
    for row, line in enumerate(lines):
        notes.write(row, 0, line, header if row == 0 else None)
    coeff = wb.add_worksheet("Coefficients")
    coeff.set_column("A:A", 12)
    coeff.set_column("B:B", 36)
    coeff.set_column("C:G", 20)
    coeff.write_row(0, 0, ["Coefficient", "Meaning", "Old signed", "Old hex", "New signed", "New hex", "Unrounded candidate"], header)
    meanings = ["Offset", "Gain denominator", "Second-order nonlinearity", "Third-order nonlinearity", "Offset temperature first-order", "Offset temperature second-order", "Gain temperature first-order", "Gain temperature second-order"]
    coeff.write_row(11, 0, ["Y gain a", a])
    coeff.write_row(12, 0, ["Y offset b", b])
    for i in range(8):
        r = i + 2
        coeff.write_row(i + 1, 0, [f"c{i}", meanings[i], old[i], f"{old[i] & 65535:04X}"])
        coeff.write(i + 1, 5, f"{new[i] & 65535:04X}")
        if i in (0, 4, 5):
            paired = {0: 1, 4: 6, 5: 7}[i]
            form = f"=C{r}+($B$13/$B$12)*C{paired+2}"
            raw = old[i] + b / a * old[paired]
        elif i in (1, 6, 7):
            form, raw = f"=C{r}/$B$12", old[i] / a
        else:
            form, raw = f"=C{r}", old[i]
        coeff.write_formula(i + 1, 6, form, number, raw)
        coeff.write_formula(i + 1, 4, f"=ROUND(G{r},0)", None, new[i])
    fn = wb.add_worksheet("Function")
    fn.set_column("A:A", 44)
    fn.set_column("B:C", 24)
    fn.write_row(0, 0, ["Input / result", "Old calibration", "New calibration"], header)
    z = inverse(2479.5 / 32768) * old[1] - old[0]
    for row, label, value in [(1, "ADC resolution rADC", 13), (2, "Raw corrected pressure Z_p", z), (3, "Raw corrected temperature Z_T1", 0), (4, "Sensor VDDA (V)", 5)]:
        fn.write(row, 0, label)
        fn.write(row, 1, value, editable)
    labels = ["Normalized raw temperature t", "Numerator N", "Denominator D", "Intermediate Y", "Conditioned P", "Ideal calibrated count", "11-bit DAC code (model)", "Predicted analog voltage (V)"]
    for row, label in enumerate(labels, 6):
        fn.write(row, 0, label)
    for col, cc in [(1, "C"), (2, "E")]:
        x = "B" if col == 1 else "C"
        vals = old if col == 1 else new
        yy = (z + vals[0]) / vals[1]
        pp = f(yy)
        forms = ["=$B$4/2^($B$2-1)", f"=$B$3+Coefficients!{cc}2+Coefficients!{cc}6*{x}7+Coefficients!{cc}7*{x}7^2", f"=Coefficients!{cc}3+Coefficients!{cc}8*{x}7+Coefficients!{cc}9*{x}7^2", f"={x}8/{x}9", f"={x}10*(1-Coefficients!{cc}4/32768-Coefficients!{cc}5/32768)+Coefficients!{cc}4*{x}10^2/32768+Coefficients!{cc}5*{x}10^3/32768", f"={x}11*32768", f"=MAX(0,MIN(2047,INT({x}12/16)))", f"={x}13*$B$5/2048"]
        cached = [0, z + vals[0], vals[1], yy, pp, pp * 32768, int(pp * 2048), int(pp * 2048) * 5 / 2048]
        for row, (form, result) in enumerate(zip(forms, cached), 6):
            fn.write_formula(row, col, form, number, result)
    curve = wb.add_worksheet("Curve")
    curve.set_column("A:H", 22)
    curve.write_row(0, 0, ["bar absolute", "Old count model", "Old Y reconstructed", "Raw Z_p at t=0", "New Y", "New count predicted", "Old DAC V", "New DAC V"], header)
    for i in range(101):
        row, r = i + 1, i + 2
        p = i / 100
        count = 2479.5 + p * (29542.625 - 2479.5)
        y = inverse(count / 32768)
        zp = y * old[1] - old[0]
        yn = (zp + new[0]) / new[1]
        cn = f(yn) * 32768
        curve.write(row, 0, p)
        curve.write_formula(row, 1, f"=2479.5+A{r}*(29542.625-2479.5)", number, count)
        # Stable inverse of q*y^2+(1-q)*y=P; original c3 is zero.
        curve.write_formula(row, 2, f"=2*(B{r}/32768)/((1-Coefficients!C4/32768)+SQRT((1-Coefficients!C4/32768)^2+4*(Coefficients!C4/32768)*(B{r}/32768)))", number, y)
        curve.write_formula(row, 3, f"=C{r}*Coefficients!C3-Coefficients!C2", number, zp)
        curve.write_formula(row, 4, f"=(D{r}+Coefficients!E2)/Coefficients!E3", number, yn)
        curve.write_formula(row, 5, f"=(E{r}*(1-Coefficients!E4/32768-Coefficients!E5/32768)+Coefficients!E4*E{r}^2/32768+Coefficients!E5*E{r}^3/32768)*32768", number, cn)
        for col, letter, v in [(6, "B", count), (7, "F", cn)]:
            curve.write_formula(row, col, f"=MAX(0,MIN(2047,INT({letter}{r}/16)))*Function!$B$5/2048", number, int(v / 16) * 5 / 2048)
    chart = wb.add_chart({"type": "scatter", "subtype": "straight"})
    for col, name in [(6, "Old endpoint model"), (7, "New RAM prediction")]:
        chart.add_series({"name": name, "categories": ["Curve", 1, 0, 101, 0], "values": ["Curve", 1, col, 101, col]})
    chart.set_x_axis({"name": "bar absolute"})
    chart.set_y_axis({"name": "Analog voltage (V)"})
    curve.insert_chart("J2", chart)
print(path)
