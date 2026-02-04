import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: "Rules"

    property var rulesModel: []
    property var currentRule: ({
        id: "",
        name: "",
        description: "",
        scope: "event",
        severity: "info",
        enabled: true,
        categoryEquals: "",
        riskLevelAtLeast: "",
        packageNameContains: "",
        activeFrom: "",
        activeTo: "",
        extra: ({})
    })

    function resetRule() {
        root.currentRule = ({
            id: "",
            name: "",
            description: "",
            scope: "event",
            severity: "info",
            enabled: true,
            categoryEquals: "",
            riskLevelAtLeast: "",
            packageNameContains: "",
            activeFrom: "",
            activeTo: "",
            extra: ({})
        })
    }

    function selectRule(rule) {
        root.currentRule = JSON.parse(JSON.stringify(rule))
    }

    function syncCombos() {
        if (!scopeCombo || !severityCombo || !riskCombo) {
            return
        }
        const scopeValue = root.currentRule.scope || "event"
        for (var i = 0; i < scopeCombo.model.length; ++i) {
            if (scopeCombo.model[i].value === scopeValue) {
                scopeCombo.currentIndex = i
                break
            }
        }

        const severityValue = root.currentRule.severity || "info"
        for (var j = 0; j < severityCombo.model.length; ++j) {
            if (severityCombo.model[j].value === severityValue) {
                severityCombo.currentIndex = j
                break
            }
        }

        const riskValue = root.currentRule.riskLevelAtLeast || ""
        for (var k = 0; k < riskCombo.model.length; ++k) {
            if (riskCombo.model[k].value === riskValue) {
                riskCombo.currentIndex = k
                break
            }
        }
    }

    onCurrentRuleChanged: syncCombos()

    Component.onCompleted: {
        if (watchClient) {
            watchClient.loadRules()
        }
    }

    Connections {
        target: watchClient
        function onRulesLoaded(rules) {
            root.rulesModel = rules
        }
        function onErrorOccurred(message) {
            console.warn("Watch rules error:", message)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "New rule"
                onClicked: root.resetRule()
            }

            Button {
                text: "Refresh"
                onClicked: watchClient.loadRules()
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Kirigami.Units.largeSpacing

            ListView {
                id: rulesList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 320
                clip: true
                model: root.rulesModel

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: (modelData.name && modelData.name.length > 0)
                        ? (modelData.name + " (" + modelData.scope + ", " + modelData.severity + ")")
                        : "Unnamed rule"
                    checkable: true
                    checked: root.currentRule && modelData.id === root.currentRule.id
                    onClicked: root.selectRule(modelData)
                }
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextField {
                    Kirigami.FormData.label: "Name"
                    text: root.currentRule.name || ""
                    onTextChanged: root.currentRule.name = text
                }

                TextArea {
                    Kirigami.FormData.label: "Description"
                    text: root.currentRule.description || ""
                    wrapMode: Text.Wrap
                    onTextChanged: root.currentRule.description = text
                }

                CheckBox {
                    Kirigami.FormData.label: "Enabled"
                    checked: root.currentRule.enabled === undefined ? true : root.currentRule.enabled
                    onToggled: root.currentRule.enabled = checked
                }

                ComboBox {
                    id: scopeCombo
                    Kirigami.FormData.label: "Scope"
                    model: [
                        { text: "Event", value: "event" },
                        { text: "Snapshot", value: "snapshot" }
                    ]
                    textRole: "text"
                    onActivated: root.currentRule.scope = model[index].value
                    Component.onCompleted: {
                        const current = root.currentRule.scope || "event"
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].value === current) {
                                currentIndex = i
                                break
                            }
                        }
                    }
                }

                ComboBox {
                    id: severityCombo
                    Kirigami.FormData.label: "Severity"
                    model: [
                        { text: "Info", value: "info" },
                        { text: "Warning", value: "warning" },
                        { text: "Critical", value: "critical" }
                    ]
                    textRole: "text"
                    onActivated: root.currentRule.severity = model[index].value
                    Component.onCompleted: {
                        const current = root.currentRule.severity || "info"
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].value === current) {
                                currentIndex = i
                                break
                            }
                        }
                    }
                }

                TextField {
                    Kirigami.FormData.label: "Category equals"
                    text: root.currentRule.categoryEquals || ""
                    placeholderText: "kernel, gpu_driver, firmware"
                    onTextChanged: root.currentRule.categoryEquals = text
                }

                ComboBox {
                    id: riskCombo
                    Kirigami.FormData.label: "Risk level at least"
                    model: [
                        { text: "Any", value: "" },
                        { text: "Info", value: "info" },
                        { text: "Important", value: "important" },
                        { text: "Critical", value: "critical" }
                    ]
                    textRole: "text"
                    onActivated: root.currentRule.riskLevelAtLeast = model[index].value
                    Component.onCompleted: {
                        const current = root.currentRule.riskLevelAtLeast || ""
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].value === current) {
                                currentIndex = i
                                break
                            }
                        }
                    }
                }

                TextField {
                    Kirigami.FormData.label: "Package contains"
                    text: root.currentRule.packageNameContains || ""
                    onTextChanged: root.currentRule.packageNameContains = text
                }

                TextField {
                    Kirigami.FormData.label: "Active from"
                    text: root.currentRule.activeFrom || ""
                    placeholderText: "HH:MM"
                    onTextChanged: root.currentRule.activeFrom = text
                }

                TextField {
                    Kirigami.FormData.label: "Active to"
                    text: root.currentRule.activeTo || ""
                    placeholderText: "HH:MM"
                    onTextChanged: root.currentRule.activeTo = text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Button {
                        text: "Save"
                        enabled: (root.currentRule.name || "").length > 0
                        onClicked: {
                            watchClient.saveRule(root.currentRule)
                            watchClient.loadRules()
                        }
                    }

                    Button {
                        text: "Delete"
                        enabled: (root.currentRule.id || "").length > 0
                        onClicked: {
                            watchClient.deleteRule(root.currentRule.id)
                            root.resetRule()
                            watchClient.loadRules()
                        }
                    }
                }
            }
        }
    }
}
