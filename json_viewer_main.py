""" a json viewer for view lua profiler report in json format """
import sys

from PySide2.QtWidgets import QApplication
from json_viewer_window_logic import MainWindow


def main():
    """ the main function """
    app = QApplication(sys.argv)
    form = MainWindow()
    form.show()
    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
