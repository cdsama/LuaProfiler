""" logic of window """


import json
import math

from PySide2.QtCore import Qt
from PySide2.QtGui import (QBrush, QColor, QDragEnterEvent, QDropEvent,
                           QFontDatabase)
from PySide2.QtWidgets import QHeaderView, QMainWindow, QTreeWidgetItem

from json_viewer_window import Ui_MainWindow


def on_item_double_clicked(item):
    """ expand all children item """
    if not item.isExpanded():
        while item.childCount() != 0:
            item = item.child(0)
            item.setExpanded(True)


class MainWindow(QMainWindow):
    """ main window logic class """

    def __init__(self):
        super().__init__()
        self.window = Ui_MainWindow()
        self.window.setupUi(self)
        self.window.treeWidget.header().setSectionResizeMode(
            QHeaderView.ResizeToContents)
        self.window.treeWidget.setHeaderLabels(
            ["Function Name",
             "Count",
             "Total (nanoseconds)",
             "Self (nanoseconds)",
             "Children (nanoseconds)"])
        self.setAcceptDrops(True)
        default_font_size = self.window.treeWidget.font().pointSize()
        self.mono_space_font = QFontDatabase.systemFont(
            QFontDatabase.FixedFont)
        self.mono_space_font.setPointSize(default_font_size)
        self.top_item = None
        self.window.treeWidget.itemDoubleClicked.connect(
            on_item_double_clicked)

    def dragEnterEvent(self, event: QDragEnterEvent):
        mime_data = event.mimeData()
        if mime_data.hasUrls() and mime_data.urls()[0].toLocalFile():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent):
        mime_data = event.mimeData()
        url = mime_data.urls()[0]
        file_name = url.toLocalFile()
        if file_name.endswith("lua_profile_json.txt"):
            self.handle_json_file(file_name)

    def handle_json_file(self, file_name: str):
        """ read file to json """
        with open(file_name, 'r') as file:
            json_dict = json.load(file)
            self.handle_json_to_tree(json_dict)

    def handle_json_to_tree(self, root):
        """ change json dict to item """
        if self.top_item:
            root_item = self.window.treeWidget.invisibleRootItem()
            root_item.removeChild(self.top_item)
            self.top_item = None
        stack = []
        item_stack = []
        current_stack = 0
        stack.append(root)
        total_time = root["total_time"]
        while stack:
            current = stack.pop()
            if current is None:
                current_stack = current_stack - 1
                continue
            while len(item_stack) > current_stack:
                item_stack.pop()
            item = QTreeWidgetItem(None, [current["function_name"],
                                          str(current["count"]),
                                          str(current["total_time"]),
                                          str(current["self_time"]),
                                          str(current["children_time"])])
            current_total_time = current["total_time"]
            color = 255
            if 0 < current_total_time <= total_time:
                color = 255*(1 - math.sqrt(current_total_time / total_time))
            brush = QBrush(QColor(255, color, color))
            item.setBackground(0, brush)
            for column_index in [1, 2, 3, 4]:
                item.setTextAlignment(column_index, Qt.AlignRight)
                item.setBackground(column_index, brush)
                item.setFont(column_index, self.mono_space_font)
            if current_stack == 0:
                self.window.treeWidget.addTopLevelItem(item)
                self.top_item = item
            else:
                item_stack[-1].addChild(item)
            item_stack.append(item)
            if "children" not in current:
                continue
            stack.append(None)
            current_stack = current_stack + 1
            for child in current["children"][::-1]:
                stack.append(child)
