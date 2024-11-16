import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
import numpy as np
import sys

# Initialize the Qt application
app = QtWidgets.QApplication([])

# Create a layout widget
win = pg.GraphicsLayoutWidget(show=True, title="Multiple Lines in Multiple Subplots")
win.resize(1000, 600)
win.setWindowTitle("Multiple Lines in Multiple Subplots Example")

# Number of subplots and lines per subplot
num_subplots = 3
num_lines = 3

# Lists to hold plot items and their lines
subplots = []
all_lines = []

# Create multiple subplots with multiple lines in each
for i in range(num_subplots):
    plot = win.addPlot(row=0, col=i)
    plot.setTitle(f"Subplot {i + 1}")
    subplots.append(plot)
    
    # Create lines for each subplot
    lines = []
    for j in range(num_lines):
        curve = plot.plot(pen=(j, num_lines))  # Different color for each line
        lines.append(curve)
    all_lines.append(lines)  # Add the lines of this subplot to all_lines

# Function to update the plots with new data
def update():
    x = np.linspace(0, 10, 100)
    for i, lines in enumerate(all_lines):
        for j, line in enumerate(lines):
            # Generate data for each line
            y = np.sin(x + j * np.pi / 5 + update.counter / 10 + i * np.pi / 3)  # Different phase per line and subplot
            line.setData(x, y)
    update.counter += 1

# Initialize a counter for the update function
update.counter = 0

# Set up a timer to periodically call the update function
timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(50)  # Update every 50 ms

# Start the Qt event loop
if __name__ == "__main__":
    sys.exit(app.exec_())