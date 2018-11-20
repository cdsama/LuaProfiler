""" logic of window """


import json
from PySide2.QtWidgets import QMainWindow, QHeaderView, QTreeWidgetItem
from PySide2.QtGui import QDragEnterEvent, QDropEvent

from json_viewer_window import Ui_MainWindow


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
