using UnityEngine;
using UnityEditor;

namespace MidiJack {

    public class MidiOutTester : EditorWindow {
        public int channel;
        public int noteNumber;
        public int noteVelocity;

        private int selectedDeviceID;

        #region Custom Editor Window Code

        [MenuItem("Window/MIDI Jack/MIDI Out Tester")]
        public static void ShowWindow () {
            EditorWindow.GetWindow<MidiOutTester>("MIDI Jack");
        }

        private void OnGUI () {
            if (GUILayout.Button((MidiMaster.allOutDevices == null || MidiMaster.allOutDevices.Length == 0 ? "Get" : "Refresh") + " device list"))
            {
                MidiMaster.RegisterOutDevices();
            }

            if (MidiMaster.allOutDevices != null && MidiMaster.allOutDevices.Length > 0)
            {
                selectedDeviceID = EditorGUILayout.Popup("Device", selectedDeviceID, MidiMaster.allOutDevices);

                channel = EditorGUILayout.IntField("Channel", channel);
                noteNumber = EditorGUILayout.IntField("Note Number", noteNumber);
                noteVelocity = EditorGUILayout.IntField("Note Velocity", noteVelocity);

                if (GUILayout.Button("Send"))
                {
                    Debug.Log("Sending Test Note");
                    MidiMaster.SendNote(selectedDeviceID, channel, noteNumber, noteVelocity);
                }

                if (GUILayout.Button("Reset Channel"))
                {
                    Debug.Log("Resetting channel " + channel + " on " + MidiMaster.allOutDevices[selectedDeviceID] + ".");
                    MidiMaster.ResetChannel(selectedDeviceID, channel);
                }

                if (GUILayout.Button("Reset Device"))
                {
                    if (EditorUtility.DisplayDialog("Warning!", "You're about to reset this device. Are you sure you want to? This process may take some time.", "Yep, do it.", "Nope, please don't."))
                    {
                        MidiMaster.ResetDevice(selectedDeviceID);
                    }
                }

            }
            else
            {
                EditorGUILayout.HelpBox("No MIDI out devices found!", MessageType.Warning);
            }

        }

        #endregion

    }
}