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


HEADER_LABELS = ["Function Name",
                 "Count",
                 "Total (nanoseconds)",
                 "Self (nanoseconds)",
                 "Children (nanoseconds)"]


def init_tree_view(tree_widget):
    """ init tree view """
    tree_widget.header().setSectionResizeMode(
        QHeaderView.ResizeToContents)
    tree_widget.header().setSectionsMovable(False)
    tree_widget.header().setSectionsClickable(True)
    tree_widget.setHeaderLabels(HEADER_LABELS)


def get_brush(val, max_val):
    """ get color brush for item value """
    color = 255
    if 0 < val <= max_val:
        color = 255*(1 - math.pow(val / max_val, 0.75))
    return QBrush(QColor(255, color, color))


class MainWindow(QMainWindow):
    """ main window logic class """

    def __init__(self):
        super().__init__()
        self.window = Ui_MainWindow()
        self.window.setupUi(self)
        init_tree_view(self.window.treeWidget)
        init_tree_view(self.window.listWidget)
        self.setAcceptDrops(True)
        self.window.tabWidget.setCurrentIndex(0)
        default_font_size = self.window.treeWidget.font().pointSize()
        self.mono_space_font = QFontDatabase.systemFont(
            QFontDatabase.FixedFont)
        self.mono_space_font.setPointSize(default_font_size)
        self.tree_top_item = None
        self.window.treeWidget.itemDoubleClicked.connect(
            on_item_double_clicked)
        self.window.listWidget.header().sectionClicked.connect(
            self.section_clicked)
        self.list_dict = {}
        self.json_dict = {}
        self.total_time = 0

    def section_clicked(self, index):
        """ sort section """
        if index == 0:
            return
        self.window.listWidget.sortItems(index, Qt.DescendingOrder)

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
            self.json_dict = json.load(file)
            self.handle_dict_to_tree(self.json_dict)
            self.handle_dict_to_list(self.json_dict)
            self.add_list_to_view()

    def add_list_to_view(self):
        """ add list to view """
        self.window.listWidget.invisibleRootItem().takeChildren()
        list_data = []
        for value in self.list_dict.values():
            list_data.append(value)
        for data in list_data:
            item_data = [data["function_name"],
                         (data["count"]),
                         (data["total_time"]),
                         (data["self_time"]),
                         (data["children_time"])]
            item = QTreeWidgetItem(None, item_data)
            for column_index in [1, 2, 3, 4]:
                item.setTextAlignment(column_index, Qt.AlignRight)
                item.setBackground(column_index, get_brush(
                    item_data[column_index], self.total_time))
                item.setFont(column_index, self.mono_space_font)
                item.setData(column_index, Qt.DisplayRole,
                             item_data[column_index])
            self.window.listWidget.addTopLevelItem(item)
        self.window.listWidget.sortItems(2, Qt.DescendingOrder)

    def handle_dict_to_list(self, root):
        """ change json dict to item """
        self.list_dict.clear()
        stack = []
        if "children" in root:
            for child in root["children"]:
                stack.append(child)
        while stack:
            current = stack.pop()
            current_source = current["function_source"]
            if current_source in self.list_dict:
                old_obj = self.list_dict[current_source]
                current_name = current["function_name"]
                if old_obj["function_name"] != current_name:
                    if current_name.startswith("?"):
                        old_obj["function_name"] = current_name
                old_obj["count"] += current["count"]
                old_obj["total_time"] += current["total_time"]
                old_obj["self_time"] += current["self_time"]
                old_obj["children_time"] += current["children_time"]
            else:
                if current["function_source"]:
                    new_obj = current.copy()
                    new_obj["function_source"] = None
                    new_obj["children"] = None
                    self.list_dict[current_source] = new_obj
            if "children" in current:
                for child in current["children"]:
                    stack.append(child)

    def handle_dict_to_tree(self, root):
        """ change json dict to item """
        if self.tree_top_item:
            root_item = self.window.treeWidget.invisibleRootItem()
            root_item.removeChild(self.tree_top_item)
            self.tree_top_item = None
        stack = []
        item_stack = []
        current_stack = 0
        stack.append(root)
        self.total_time = root["total_time"]
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
            brush = get_brush(current_total_time, self.total_time)
            item.setBackground(0, brush)
            for column_index in [1, 2, 3, 4]:
                item.setTextAlignment(column_index, Qt.AlignRight)
                item.setBackground(column_index, brush)
                item.setFont(column_index, self.mono_space_font)
            if current_stack == 0:
                self.window.treeWidget.addTopLevelItem(item)
                self.tree_top_item = item
            else:
                item_stack[-1].addChild(item)
            item_stack.append(item)
            if "children" not in current:
                continue
            stack.append(None)
            current_stack = current_stack + 1
            for child in current["children"][:: -1]:
                stack.append(child)
