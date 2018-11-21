""" logic of window """


import json
import math
from PySide2.QtCore import Qt
from PySide2.QtGui import QDragEnterEvent, QDropEvent, QBrush, QColor
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
        self.ui_window = Ui_MainWindow()
        self.ui_window.setupUi(self)
        self.ui_window.treeWidget.header().setSectionResizeMode(
            QHeaderView.ResizeToContents)
        self.ui_window.treeWidget.setHeaderLabels(
            ["Function Name",
             "Count",
             "Total (nanoseconds)",
             "Self (nanoseconds)",
             "Children (nanoseconds)"])
        self.setAcceptDrops(True)
        self.top_item = None
        self.ui_window.treeWidget.itemDoubleClicked.connect(
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
            self.handle_json_dict(json_dict)

    def handle_json_dict(self, root):
        """ change json dict to item """
        if self.top_item:
            root_item = self.ui_window.treeWidget.invisibleRootItem()
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
            parent_size = current_stack
            while len(item_stack) > parent_size:
                item_stack.pop()
            item = QTreeWidgetItem(None, [current["function_name"],
                                          str(current["count"]),
                                          str(current["total_time"]),
                                          str(current["self_time"]),
                                          str(current["children_time"])])
            current_total_time = current["total_time"]
            color = 0
            if current_total_time < total_time:
                color = 255*(1 - math.sqrt(current_total_time / total_time))
            brush = QBrush(QColor(255, color, color))
            item.setBackground(0, brush)
            for column_index in [1, 2, 3, 4]:
                item.setTextAlignment(column_index, Qt.AlignRight)
                item.setBackground(column_index, brush)
            if current_stack == 0:
                self.ui_window.treeWidget.addTopLevelItem(item)
                self.top_item = item
            else:
                parent = item_stack[-1]
                parent.addChild(item)
            item_stack.append(item)
            if "children" not in current:
                continue
            stack.append(None)
            current_stack = current_stack + 1
            children = current["children"]
            for child in children[::-1]:
                stack.append(child)
