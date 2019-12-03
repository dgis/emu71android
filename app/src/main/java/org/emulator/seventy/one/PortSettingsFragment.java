package org.emulator.seventy.one;

import android.app.Activity;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.ContextMenu;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatDialogFragment;
import androidx.appcompat.widget.Toolbar;
import androidx.constraintlayout.widget.ConstraintLayout;

import org.emulator.calculator.Utils;

import java.util.ArrayList;
import java.util.Locale;
import java.util.Objects;


public class PortSettingsFragment extends AppCompatDialogFragment {
    private static final String TAG = "PortSettings";
    private boolean debug = false;

    public static final int INTENT_OPEN_ROM = 71;
    public static final int INTENT_DATA_LOAD = 72;
    public static final int INTENT_DATA_SAVE = 73;

    private Spinner spinnerSelPort;
    private AdapterView.OnItemSelectedListener spinnerSelPortOnItemSelected;
    //private int spinnerSelPortNoEvent = 0;
    private ArrayList<String> listItems = new ArrayList<>();
    private ArrayAdapter<String> adapterListViewPortData;
    private ListView listViewPortData;
    private Button buttonAdd;
    private Button buttonAbort;
    private Button buttonDelete;
    private Button buttonApply;
    private Spinner spinnerType;
    private AdapterView.OnItemSelectedListener spinnerTypeOnItemSelected;
    //private int spinnerTypeNoEvent = 0;
    private Spinner spinnerSize;
    private AdapterView.OnItemSelectedListener spinnerSizeOnItemSelected;
    //private int spinnerSizeNoEvent = 0;
    private Spinner spinnerChips;
    private EditText editTextFile;
    private Button buttonBrowse;
    private Spinner spinnerHardAddr;
    private Button buttonTCPIP;


    private int nActPort = 0;					// the actual port
    private int nUnits = 0;						// no. of applied port units in the actual port slot
    private String configFilename = "";



    //enum PORT_DATA_TYPE {
    private static final int PORT_DATA_INDEX = 0;
    private static final int PORT_DATA_APPLY = 1;
    private static final int PORT_DATA_TYPE = 2;
    private static final int PORT_DATA_BASE = 3;
    private static final int PORT_DATA_SIZE = 4;
    private static final int PORT_DATA_CHIPS = 5;
    private static final int PORT_DATA_DATA = 6;
    private static final int PORT_DATA_FILENAME = 7;
    private static final int PORT_DATA_ADDR_OUT = 8;
    private static final int PORT_DATA_PORT_OUT = 9;
    private static final int PORT_DATA_PORT_IN = 10;
    private static final int PORT_DATA_TCP_ADDR_OUT = 11;
    private static final int PORT_DATA_TCP_PORT_OUT = 12;
    private static final int PORT_DATA_TCP_PORT_IN = 13;
    private static final int PORT_DATA_NEXT_INDEX = 14;
    private static final int PORT_DATA_EXIST = 15;
    private static final int PORT_DATA_IRAMSIG = 16;
    private int nOldState;
    private boolean isDismiss;

    public static native int editPortConfigStart();
    public static native void editPortConfigEnd(int nOldState);
    public static native int getPortCfgModuleIndex(int port);
    public static native void loadCurrPortConfig();
    public static native void saveCurrPortConfig();
    public static native void cleanup();
    public static native int getPortCfgInteger(int port, int portIndex, int portDataType);
    public static native String getPortCfgString(int port, int portIndex, int portDataType);
    //public static native char[] getPortCfgData(int port, int portIndex, int portDataType);
    public static native boolean setPortCfgInteger(int port, int portIndex, int portDataType, int value);
    public static native boolean setPortCfgString(int port, int portIndex, int portDataType, String value);
    public static native boolean setPortCfgBytes(int port, int portIndex, int portDataType, char[] value);
    public static native void setPortChanged(int port, int changed);
    public static native void addNewPort(int port, int nItem);
    public static native void configModuleAbort(int nActPort);
    public static native void configModuleDelete(int nActPort, int nItemSelectedModule);
    public static native boolean applyPort(int nPort, int portDataType, String portDataFilename, int portDataSize, int portDataHardAddr, int portDataChips);
    public static native boolean dataLoad(int port, int portIndex, String filename);
    public static native boolean dataSave(int port, int portIndex, String filename);
    public static native void modifyOriginalTCPData(int port, int portIndex);


    public PortSettingsFragment() {
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setStyle(AppCompatDialogFragment.STYLE_NO_FRAME, android.R.style.Theme_Material);
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        Window window = dialog.getWindow();
        if(window != null)
            window.requestFeature(Window.FEATURE_NO_TITLE);
        return dialog;
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {

        String title = getString(R.string.dialog_port_configuration_title);
        Dialog dialog = getDialog();
        if(dialog != null)
            dialog.setTitle(title);

        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_port_settings, container, false);

        isDismiss = false;
        nOldState = editPortConfigStart();


        Toolbar toolbar = view.findViewById(R.id.my_toolbar);
        toolbar.setTitle(title);
        toolbar.setNavigationIcon(R.drawable.ic_keyboard_backspace_white_24dp);
        toolbar.setNavigationOnClickListener(v -> {
            dismiss();
        });
        toolbar.inflateMenu(R.menu.fragment_port_settings);
        toolbar.setOnMenuItemClickListener(item -> {
            if(item.getItemId() == R.id.menu_port_settings_save) {
                saveCurrPortConfig();
                dismiss();
            }
            return true;
        });
        setMenuVisibility(true);



        // load current port data structure
        loadCurrPortConfig();

        // init port combo box
        spinnerSelPort = view.findViewById(R.id.spinnerSelPort);
        spinnerSelPort.setSelection(1);
        spinnerSelPortOnItemSelected = new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
//                if(spinnerSelPortNoEvent > 0) {
//                    spinnerSelPortNoEvent--;
//                    return;
//                }

//                if (psPortCfg[nActPort] != NULL)
//                {
//                    if ((*CfgModule(nActPort))->bApply == FALSE)
//                    {
//                        // delete the not applied module
//                        DelPortCfg(nActPort);
//                    }
//                }
                nActPort = position;
                ShowPortConfig(nActPort);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        };
        spinnerSelPort.setOnItemSelectedListener(spinnerSelPortOnItemSelected);

        adapterListViewPortData = new ArrayAdapter<>(Objects.requireNonNull(getContext()), R.layout.simple_list_item_1, listItems);
        listViewPortData = view.findViewById(R.id.listViewPortData);
        listViewPortData.setAdapter(adapterListViewPortData);
//        listViewPortData.setOnItemClickListener((parent, view1, position, id) -> {
//
//        });
        registerForContextMenu(listViewPortData);

        buttonAdd = view.findViewById(R.id.buttonAdd);
        buttonAdd.setOnClickListener(v -> {
            int nItem = listViewPortData.getSelectedItemPosition();
            addNewPort(nActPort, nItem);
            OnAddPort(nActPort);
        });

        buttonAbort = view.findViewById(R.id.buttonAbort);
        buttonAbort.setOnClickListener(v -> {
            int portModuleIndex = getPortCfgModuleIndex(nActPort);
            if(portModuleIndex != -1) {
                int bApply = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_APPLY);
                if(bApply == 0 /*FALSE*/)
                    configModuleAbort(nActPort);
            }

            ShowPortConfig(nActPort);
        });

        buttonDelete = view.findViewById(R.id.buttonDelete);
        buttonDelete.setOnClickListener(v -> {
            int nItem = listViewPortData.getSelectedItemPosition();
            configModuleDelete(nActPort, nItem);
            ShowPortConfig(nActPort);
        });

        buttonApply = view.findViewById(R.id.buttonApply);
        buttonApply.setOnClickListener(v -> {
            // apply port data
            if (!ApplyPort(nActPort)) {
                OnAddPort(nActPort);
                return;
            }
            ShowPortConfig(nActPort);
        });

        spinnerType = view.findViewById(R.id.spinnerType);
        //spinnerTypeNoEvent++;
        spinnerType.setSelection(0);
        spinnerTypeOnItemSelected = new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
//                if(spinnerTypeNoEvent > 0) {
//                    spinnerTypeNoEvent--;
//                    return;
//                }
                if(nActPort == -1) return;

                // fetch module in queue to configure
                int portModuleIndex = getPortCfgModuleIndex(nActPort);
                if(portModuleIndex == -1) return;

                // module type combobox
                int type = position + 1;
                setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_TYPE, type);
                setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_BASE, type == 3 /*TYPE_HRD*/
								  ? 0xE0000
								  : 0x00000);
                OnAddPort(nActPort);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        };
        spinnerType.setOnItemSelectedListener(spinnerTypeOnItemSelected);

                spinnerSize = view.findViewById(R.id.spinnerSize);
        //spinnerSizeNoEvent++;
        spinnerSize.setSelection(7);
        spinnerSizeOnItemSelected = new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
//                if(spinnerSizeNoEvent > 0) {
//                    spinnerSizeNoEvent--;
//                    return;
//                }

                if(nActPort == -1) return;

                // fetch module in queue to configure
                int portModuleIndex = getPortCfgModuleIndex(nActPort);
                if(portModuleIndex == -1) return;

                // fetch combo box selection
                int size = getChipSizeFromSelectedPosition(position);

                // get new size
                setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_SIZE, size);

                // reconfigure dialog settings
                OnAddPort(nActPort);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        };
        spinnerSize.setOnItemSelectedListener(spinnerSizeOnItemSelected);

        spinnerChips = view.findViewById(R.id.spinnerChips);
        spinnerChips.setSelection(0);

        editTextFile = view.findViewById(R.id.editTextFile);

        buttonBrowse = view.findViewById(R.id.buttonBrowse);
        buttonBrowse.setOnClickListener(v -> OnBrowse());

        spinnerHardAddr = view.findViewById(R.id.spinnerHardAddr);
        spinnerHardAddr.setSelection(1);

        buttonTCPIP = view.findViewById(R.id.buttonTCPIP);
        buttonTCPIP.setOnClickListener(v -> {
            if(nActPort == -1) return;

            int portModuleIndex = getPortCfgModuleIndex(nActPort); // module in queue to configure
            if(portModuleIndex == -1) return;

            // must be a HPIL module
            int type = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_TYPE);
            if(type == 4 /*TYPE_HPIL*/)
                OnEditTcpIpSettings(portModuleIndex, new Runnable() {
                    @Override
                    public void run() {

                    }
                });
        });

        return view;
    }

    @Override
    public void onDismiss(@NonNull DialogInterface dialog) {
        if(!isDismiss) {
            this.isDismiss = true;
            cleanup();
            editPortConfigEnd(nOldState);
        }
        super.onDismiss(dialog);
    }

    @Override
    public void onCreateContextMenu(@NonNull ContextMenu menu, @NonNull View v, @Nullable ContextMenu.ContextMenuInfo menuInfo) {
        super.onCreateContextMenu(menu, v, menuInfo);
        if (v.getId() == R.id.listViewPortData) {
            Objects.requireNonNull(getActivity()).getMenuInflater().inflate(R.menu.fragment_port_settings_contextual_menu, menu);
            AdapterView.AdapterContextMenuInfo contextMenuInfo = (AdapterView.AdapterContextMenuInfo)menuInfo;
            if(contextMenuInfo != null) {
                int nModuleIndex = contextMenuInfo.position;
                int type = getPortCfgInteger(nActPort, nModuleIndex, PORT_DATA_TYPE);
                //MenuItem menuItemDelete = menu.findItem(R.id.contextual_menu_port_settings_delete);
                MenuItem menuItemDataLoad = menu.findItem(R.id.contextual_menu_port_settings_data_load);
                MenuItem menuItemDataSave = menu.findItem(R.id.contextual_menu_port_settings_data_save);
                MenuItem menuItemTcpipSettings = menu.findItem(R.id.contextual_menu_port_settings_tcpip_settings);
                if(type != 4 /*TYPE_HPIL*/) {
                    // RAM with data
                    // independent RAM signature?
                    boolean iramsig = getPortCfgInteger(nActPort, nModuleIndex, PORT_DATA_IRAMSIG) != 0;
                    if(menuItemDataLoad != null) menuItemDataLoad.setEnabled(iramsig);
                    if(menuItemDataSave != null) menuItemDataSave.setEnabled(iramsig);
                    if(menuItemTcpipSettings != null) menuItemTcpipSettings.setEnabled(false);
                } else {
                    if(menuItemDataLoad != null) menuItemDataLoad.setEnabled(false);
                    if(menuItemDataSave != null) menuItemDataSave.setEnabled(false);
                    if(menuItemTcpipSettings != null) menuItemTcpipSettings.setEnabled(true);
                }
            }
        }

        MenuItem.OnMenuItemClickListener listener = item -> {
            onContextItemSelected(item);
            return true;
        };

        for (int i = 0, n = menu.size(); i < n; i++)
            menu.getItem(i).setOnMenuItemClickListener(listener);
    }

    @Override
    public boolean onContextItemSelected(@NonNull MenuItem item) {
        AdapterView.AdapterContextMenuInfo info = (AdapterView.AdapterContextMenuInfo) item.getMenuInfo();
        int portModuleIndex = info.position;
        switch(item.getItemId()) {
            case R.id.contextual_menu_port_settings_delete:
                configModuleDelete(nActPort, portModuleIndex);
                ShowPortConfig(nActPort);
                return true;
            case R.id.contextual_menu_port_settings_data_load:
                OnPortCfgDataLoad(portModuleIndex);
                break;
            case R.id.contextual_menu_port_settings_data_save:
                OnPortCfgDataSave(portModuleIndex);
                break;
            case R.id.contextual_menu_port_settings_tcpip_settings:
                OnEditTcpIpSettings(portModuleIndex, () -> {
                    // Modify the original data to avoid a configuration changed on the whole module
                    modifyOriginalTCPData(nActPort, portModuleIndex);

                    ShowPortConfig(nActPort);

                });
                break;
        }
        return super.onContextItemSelected(item);
    }

    private void ShowPortConfig(int port) {

        // clear configuration input fields
        configFilename = "";
        editTextFile.setText("");

        // enable configuration list box
        listViewPortData.setEnabled(true);

        // button control
        buttonAdd.setEnabled(true);
        buttonDelete.setEnabled(true); //TODO
        buttonApply.setEnabled(false);
        spinnerType.setEnabled(false);
        spinnerSize.setEnabled(false);
        spinnerChips.setEnabled(false);
        editTextFile.setEnabled(false);
        buttonBrowse.setEnabled(false);
        spinnerHardAddr.setEnabled(false);

        // fill the list box with the current data
        listItems.clear();
        adapterListViewPortData.notifyDataSetChanged();
        Utils.setListViewHeightBasedOnChildren(listViewPortData);

        int portIndex = 0;
        int exist = getPortCfgInteger(port, portIndex, PORT_DATA_EXIST);
        if(exist > 0) {

            do {
                String buffer = "";

                // module type
                String[] modType = getResources().getStringArray(R.array.port_configuration_mod_type);
                int type = getPortCfgInteger(port, portIndex, PORT_DATA_TYPE);
                buffer += type > 0 && type <= modType.length ? modType[type - 1] : "UNKNOWN";

                buffer += ", ";

                // hard wired address
                if (type == 3) { //TYPE_HRD
                    int base = getPortCfgInteger(port, portIndex, PORT_DATA_BASE);
                    buffer += String.format("%05X, ", base);
                }

                // size + no. of chips
                int nIndex = getPortCfgInteger(port, portIndex, PORT_DATA_SIZE) / 2048;
                int chips = getPortCfgInteger(port, portIndex, PORT_DATA_CHIPS);
                if (nIndex == 0)
                    buffer += String.format(Locale.US, "512B (%d)", chips);
                else
                    buffer += String.format(Locale.US, "%dK (%d)", nIndex, chips);

                // filename
                String fileName = getPortCfgString(port, portIndex, PORT_DATA_FILENAME);
                if (fileName != null && fileName.length() > 0) { // given filename
                    buffer += ", \"";
                    //buffer += fileName.substring(Math.max(0, fileNameLength - 36), fileNameLength);
                    String displayName = fileName;
                    try {
                        displayName = Utils.getFileName(getContext(), fileName);
                    } catch(Exception e) {
                        // Do nothing
                    }
                    int fileNameLength = displayName.length();
                    buffer += displayName.substring(Math.max(0, fileNameLength - 36), fileNameLength);
                    buffer += "\"";
                }

                // tcp/ip configuration
                if (type == 4) { //TYPE_HPIL
                    String lpszAddrOut = getPortCfgString(port, portIndex, PORT_DATA_ADDR_OUT);
                    int wPortOut = getPortCfgInteger(port, portIndex, PORT_DATA_PORT_OUT);
                    int wPortIn = getPortCfgInteger(port, portIndex, PORT_DATA_PORT_IN);
                    buffer += String.format(Locale.US, ", \"%s\", %d, %d", lpszAddrOut, wPortOut, wPortIn);
                    ++nUnits;                        // HPIL needs two entries (HPIL mailbox & ROM)
                }

                adapterListViewPortData.add(buffer);
                Utils.setListViewHeightBasedOnChildren(listViewPortData);

                exist = getPortCfgInteger(port, ++portIndex, PORT_DATA_EXIST);
            } while (exist > 0);
        }
    }

    private void OnAddPort(int nActPort) {

        // module in queue to configure
        int portModuleIndex = getPortCfgModuleIndex(nActPort);

        setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_APPLY, 0); // module not applied


        // disable configuration list box
        listViewPortData.setEnabled(false);

        // button control
        buttonAdd.setEnabled(false);
        buttonDelete.setEnabled(true);
        buttonApply.setEnabled(true);

        // "Delete" button has now the meaning of "Abort"
        //SetDlgItemText(hDlg,IDC_CFG_DEL,_T("A&bort"));

        // module type combobox
        int type = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_TYPE);
        //spinnerTypeNoEvent++;
        spinnerType.setOnItemSelectedListener(null);
        spinnerType.setSelection(type - 1);
        spinnerType.setOnItemSelectedListener(spinnerTypeOnItemSelected);
        spinnerType.setEnabled(true);

        // size combobox
        int sizeIndex = 0; // Datafile
        if (type == 1 /*TYPE_RAM*/) {
            int size = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_SIZE);
                 if(size == 1024) sizeIndex = 1; // 512 Byte
            else if(size ==     2048) sizeIndex = 2; // 1K Byte
            else if(size == 2 * 2048) sizeIndex = 3; // 2K Byte
            else if(size == 4 * 2048) sizeIndex = 4; // 4K Byte
            else if(size == 8 * 2048) sizeIndex = 5; // 8K Byte
            else if(size == 16 * 2048) sizeIndex = 6; // 16K Byte
            else if(size == 32 * 2048) sizeIndex = 7; // 32K Byte
            else if(size == 64 * 2048) sizeIndex = 8; // 64K Byte
            else if(size == 96 * 2048) sizeIndex = 9; // 96K Byte
            else if(size == 128 * 2048) sizeIndex = 10; // 128K Byte
            else if(size == 160 * 2048) sizeIndex = 11; // 160K Byte
            else if(size == 192 * 2048) sizeIndex = 12; // 192K Byte
            //spinnerSizeNoEvent++;
            spinnerSize.setOnItemSelectedListener(null);
            spinnerSize.setSelection(sizeIndex);
            spinnerSize.setOnItemSelectedListener(spinnerSizeOnItemSelected);
            spinnerSize.setEnabled(true);
        } else
            spinnerSize.setEnabled(false);

        // no. of chips combobox
        spinnerChips.setSelection(0); // select "Auto"
        spinnerChips.setEnabled(true);

        // enable filename when not RAM or RAM size = 0 selected
        boolean bFilename = type != 1 /*TYPE_RAM*/
                || sizeIndex == 0;
        if(!bFilename) // RAM with given size
            setPortCfgString(nActPort, portModuleIndex, PORT_DATA_FILENAME, ""); // no filename
        configFilename = getPortCfgString(nActPort, portModuleIndex, PORT_DATA_FILENAME);
        String displayName = "";
        try {
            displayName = Utils.getFileName(getContext(), configFilename);
        } catch(Exception e) {
            // Do nothing
        }
        editTextFile.setText(displayName);
        editTextFile.setEnabled(bFilename);
        buttonBrowse.setEnabled(bFilename);

        // hpil interface or hard wired address
        if (type == 4 /*TYPE_HPIL*/) { // HPIL interface

            spinnerHardAddr.setEnabled(false);

            String addrOut = getPortCfgString(nActPort, portModuleIndex, PORT_DATA_ADDR_OUT);
            if (addrOut == null) { // first call
                // init tpc/ip settings with default values
                setPortCfgString(nActPort, portModuleIndex, PORT_DATA_ADDR_OUT, "localhost");
                setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_OUT, 60001);
                setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_IN, 60000);
            }

            // activate configuration button
            buttonTCPIP.setEnabled(true);
        } else { // default

            // deactivate configuration button
            buttonTCPIP.setEnabled(false);

            if (type == 3 /*TYPE_HRD*/) { // hard wired chip
                int baseAddress = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_BASE);
                spinnerHardAddr.setSelection(baseAddress == 0 ? 0 : 1);
                spinnerHardAddr.setEnabled(true);
            }
            else
                spinnerHardAddr.setEnabled(false);
        }
    }

    private boolean ApplyPort(int nPort) {
        int portDataType = spinnerType.getSelectedItemPosition() + 1;
        String portDataFilename = configFilename;
        int portDataSize = getChipSizeFromSelectedPosition(spinnerSize.getSelectedItemPosition());
        int portDataHardAddr = (spinnerHardAddr.getSelectedItemPosition() == 0 ? 0x00000 : 0xE0000);
        int portDataChips = spinnerChips.getSelectedItemPosition();

        if(applyPort(nPort, portDataType, portDataFilename, portDataSize, portDataHardAddr, portDataChips)) {
            buttonAdd.requestFocus();
            return true;
        }
        return false;
    }

    private int getChipSizeFromSelectedPosition(int position) {
        switch (position) {
            case 0: return 0; // Datafile
            case 1: return 1024; // 512 Byte
            case 2: return     2048; // 1K Byte
            case 3: return 2 * 2048; // 2K Byte
            case 4: return 4 * 2048; // 4K Byte
            case 5: return 8 * 2048; // 8K Byte
            case 6: return 16 * 2048; // 16K Byte
            case 7: return 32 * 2048; // 32K Byte
            case 8: return 64 * 2048; // 64K Byte
            case 9: return 96 * 2048; // 96K Byte
            case 10: return 128 * 2048; // 128K Byte
            case 11: return 160 * 2048; // 160K Byte
            case 12: return 192 * 2048; // 192K Byte
        }
        return 0;
    }

    private void OnBrowse() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_TITLE, getString(R.string.filename) + "rom.bin");
        startActivityForResult(intent, INTENT_OPEN_ROM);
    }

    int portModuleIndexForDataSaveLoad = -1;
    private void OnPortCfgDataLoad(int portModuleIndex) {
        portModuleIndexForDataSaveLoad = portModuleIndex;

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_TITLE, getString(R.string.filename) + "-ram.bin");
        startActivityForResult(intent, INTENT_DATA_LOAD);
    }
    private void OnPortCfgDataSave(int portModuleIndex) {
        portModuleIndexForDataSaveLoad = portModuleIndex;

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_TITLE, getString(R.string.filename) + "-ram.bin");
        startActivityForResult(intent, INTENT_OPEN_ROM);
    }

    private void OnEditTcpIpSettings(int portModuleIndex, Runnable endCallback) {
        View view = requireActivity().getLayoutInflater().inflate(R.layout.fragment_port_settings_edit_tcp, null);
        EditText editTextAddrOut = view.findViewById(R.id.editTextAddrOut);
        String addressOut = getPortCfgString(nActPort, portModuleIndex, PORT_DATA_ADDR_OUT);
        editTextAddrOut.setText(addressOut);
        EditText editTextPortOut = view.findViewById(R.id.editTextPortOut);
        int portOut = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_OUT);
        editTextPortOut.setText("" + portOut);
        EditText editTextPortIn = view.findViewById(R.id.editTextPortIn);
        int portIn = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_IN);
        editTextPortIn.setText("" + portIn);
        new AlertDialog.Builder(Objects.requireNonNull(getContext()))
                .setTitle(R.string.fragment_port_settings_edit_tcp_title)
                .setView(view)
                .setPositiveButton(R.string.message_ok, (dialog, whichButton) -> {
                    String editTextAddrOutText = editTextAddrOut.getText().toString();
                    setPortCfgString(nActPort, portModuleIndex, PORT_DATA_ADDR_OUT, editTextAddrOutText);
                    String editTextPortOutText = editTextPortOut.getText().toString();
                    try {
                        int editTextPortOutValue = Integer.parseInt(editTextPortOutText);
                        setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_OUT, editTextPortOutValue);
                    } catch (NumberFormatException ignored) {}
                    String editTextPortInText = editTextPortIn.getText().toString();
                    try {
                        int editTextPortInValue = Integer.parseInt(editTextPortInText);
                        setPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_PORT_IN, editTextPortInValue);
                    } catch (NumberFormatException ignored) {}
                    if(endCallback != null)
                        endCallback.run();
                })
                .setNegativeButton(R.string.message_cancel, (dialog, whichButton) -> {})
                .show();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        if(resultCode == Activity.RESULT_OK && data != null) {
            if (requestCode == INTENT_OPEN_ROM) {
                Uri uri = data.getData();
                configFilename = uri.toString();
                String displayName = "";
                try {
                    displayName = Utils.getFileName(getContext(), configFilename);
                } catch(Exception e) {
                    // Do nothing
                }
                editTextFile.setText(displayName);
                Utils.makeUriPersistableReadOnly(getContext(), data, uri);
            } else if(requestCode == INTENT_DATA_LOAD && portModuleIndexForDataSaveLoad >= 0) {
                Uri uri = data.getData();
                dataLoad(nActPort, portModuleIndexForDataSaveLoad, uri.toString());
                portModuleIndexForDataSaveLoad = -1;
            } else if(requestCode == INTENT_DATA_SAVE && portModuleIndexForDataSaveLoad >= 0) {
                Uri uri = data.getData();
                dataSave(nActPort, portModuleIndexForDataSaveLoad, uri.toString());
                portModuleIndexForDataSaveLoad = -1;
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
