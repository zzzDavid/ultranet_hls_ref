# UltraNet HLS Reference Design

Original project [here](https://github.com/heheda365/ultra_net)


## Top Function
- ultranet.cpp/ultranet
## Clock
- Period: 8
- Uncertainty: defult

## Build
To do high-level synthesis on the accelerator and export it as a Vivado IP,
simply use ```make build```.

## Test the functionality using Csim
You can use c simulation to compare the results from the C model with the PyTorch one,
using ```make csim```. The results from each layer and the diff results will be dumped to ```ultranet_csim``` directory.
The command-line output would show the Sum of Squared Error (SSE) and Mean of Squared Error (MSE) of each layer.
The path to the model's state dict is hard-coded at:
```
csim.py, line26: state_dict = torch.load('../../export/4bitfs.pt', map_location = torch.device('cpu'))['model']
```
Modify it if necessary.

**Before running using your account on our server, be sure to modify the ```PROJ_ROOT``` in the makefile:**
```
Makefile line21: PROJ_ROOT = /home/yd383/dac-sdc
```
**change this to point to the root of the git repo.**

## Verify the generated RTL using Cosim
The generated RTL can be verified with a custom cosim flow: ```make cosim```.
It compares the RTL model with the C++ code to make sure they are equivalent.
NOTE: it does **not** compare the RTL model with the PyTorch model.

## reference
- https://github.com/fpgasystems/spooNN.git
- https://github.com/Xilinx/BNN-PYNQ.git
