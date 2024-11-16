import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
import numpy as np
import sys

# Initialize the Qt application
app = QtWidgets.QApplication([])

# Create a layout widget
win = pg.GraphicsLayoutWidget(show=True, title="Moving Stacked Bar Graphs in Multiple Subplots")
win.resize(1000, 600)
win.setWindowTitle("Moving Stacked Bar Graphs Example")

# Variables for managing subplots and their bar graphs
num_subplots = 3  # Fixed number of subplots
subplots = []     # Store subplots
all_stacked_bars = []  # Store stacked bar segments for each subplot
num_bars = 5       # Number of bars in each subplot
num_stacks = 3     # Number of stack segments per bar

# Function to initialize subplots with bar graph placeholders
def initialize_subplots():
    global subplots, all_stacked_bars
    subplots.clear()
    all_stacked_bars.clear()
    
    # Create subplots and initialize stacked bar holders
    for i in range(num_subplots):
        plot = win.addPlot(row=0, col=i)
        plot.setTitle(f"Subplot {i + 1}")
        plot.setYRange(0, 20)  # Set y-axis range to account for stacked bars
        plot.setXRange(0, 10)  # Set initial x-axis range
        subplots.append(plot)
        all_stacked_bars.append([])  # Empty list for stacked bars in this subplot

# Initialize subplots at the beginning
initialize_subplots()

# Function to initialize bars and stacks in each subplot
def set_num_stacked_bars(subplot_index, num_bars, num_stacks):
    global all_stacked_bars
    
    # Get the subplot and clear current bars if needed
    subplot = subplots[subplot_index]
    subplot.clear()
    all_stacked_bars[subplot_index] = []  # Reset the stacked bars list for this subplot

    # Create new stacked bars for the subplot
    x_values = np.linspace(0, 10, num_bars)  # Start with fixed x positions
    for j in range(num_bars):
        stacks = []
        base_height = 0  # Starting height for the stack
        for k in range(num_stacks):
            height = np.random.uniform(1, 5)  # Random height for each segment
            bar = pg.BarGraphItem(x=[x_values[j]], height=[height], width=0.6, y0=base_height, brush=(k, num_stacks * 1.3))
            subplot.addItem(bar)
            stacks.append(bar)
            base_height += height  # Update base height for the next stack
        all_stacked_bars[subplot_index].append(stacks)

# Function to update data in each stacked bar segment within each subplot
def update():
    for subplot_index, bars in enumerate(all_stacked_bars):
        x_shift = update.counter * 0.1  # Control the speed of movement by changing this factor
        x_values = np.linspace(0, 10, len(bars)) + x_shift  # Shift x positions for each update
        
        for bar_index, stacks in enumerate(bars):
            base_height = 0  # Reset base height for each bar's stack
            for stack_index, stack in enumerate(stacks):
                # Update the bar segment's x-position and height with new values
                new_height = np.random.uniform(1, 5)
                stack.setOpts(x=[x_values[bar_index]], height=[new_height], y0=base_height)
                base_height += new_height  # Update base height for the next segment
            
        # Update the x-axis range to keep the bars visible as they move
        subplots[subplot_index].setXRange(x_shift, x_shift + 10)

    update.counter += 1

# Initialize a counter for the update function
update.counter = 0

# Set up initial bar configuration in each subplot
for i in range(num_subplots):
    set_num_stacked_bars(i, num_bars, num_stacks)

# Set up a timer to periodically call the update function
timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(100)  # Update every 100 ms

# Start the Qt event loop
if __name__ == "__main__":
    sys.exit(app.exec_())
