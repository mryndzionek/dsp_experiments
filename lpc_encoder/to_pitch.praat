form Test command line calls
    integer Number ???
endform

Read from file: "data/" + string$(number) + ".wav"
Scale peak: 0.99
Resample: 11000, 50
To Pitch... 0.04 75.0 2200.0
Save as text file: "data/" + string$(number) + ".Pitch"

