package org.emulator.seventy.one;

import android.app.Dialog;
import android.content.DialogInterface;
import android.os.Bundle;
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


    private Spinner spinnerSelPort;
    private boolean spinnerSelPortNoEvent = false;
    private ArrayList<String> listItems = new ArrayList<>();
    private ArrayAdapter<String> adapterListViewPortData;
    private ListView listViewPortData;
    private Button buttonAdd;
    private Button buttonAbort;
    private Button buttonDelete;
    private Button buttonApply;
    private Spinner spinnerType;
    private boolean spinnerTypeNoEvent = false;
    private Spinner spinnerSize;
    private boolean spinnerSizeNoEvent = false;
    private Spinner spinnerChips;
    private EditText editTextFile;
    private Button buttonBrowse;
    private Spinner spinnerHardAddr;
    private Button buttonTCPIP;


    private int nActPort = 0;					// the actual port
    private int nUnits = 0;						// no. of applied port units in the actual port slot



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
        spinnerSelPort.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if(spinnerSelPortNoEvent) {
                    spinnerSelPortNoEvent = false;
                    return;
                }

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
        });

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
        spinnerTypeNoEvent = true;
        spinnerType.setSelection(0);
        spinnerType.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if(spinnerTypeNoEvent) {
                    spinnerTypeNoEvent = false;
                    return;
                }
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
        });

        spinnerSize = view.findViewById(R.id.spinnerSize);
        spinnerSizeNoEvent = true;
        spinnerSize.setSelection(7);
        spinnerSize.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if(spinnerSizeNoEvent) {
                    spinnerSizeNoEvent = false;
                    return;
                }

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
        });

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
                OnEditTcpIpSettings(portModuleIndex);
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
            for (int i = 0, n = menu.size(); i < n; i++) {
                MenuItem item = menu.getItem(i);
                AdapterView.AdapterContextMenuInfo info = (AdapterView.AdapterContextMenuInfo) item.getMenuInfo();
                int nItem = info.position;

                int id = item.getItemId();
                if (id == R.id.contextual_menu_port_settings_delete) {
                } else  if (id == R.id.contextual_menu_port_settings_data_load) {
                } else  if (id == R.id.contextual_menu_port_settings_data_save) {
                } else  if (id == R.id.contextual_menu_port_settings_tcpip_settings) {
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
        int nItem = info.position;
        switch(item.getItemId()) {
            case R.id.contextual_menu_port_settings_delete:
                configModuleDelete(nActPort, nItem);
                ShowPortConfig(nActPort);
                return true;
            case R.id.contextual_menu_port_settings_data_load:
                break;
            case R.id.contextual_menu_port_settings_data_save:
                break;
            case R.id.contextual_menu_port_settings_tcpip_settings:
                break;
        }
        return super.onContextItemSelected(item);
    }

    private void ShowPortConfig(int port) {

        // clear configuration input fields
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
                    buffer += ", 0\"";
                    int fileNameLength = fileName.length();
                    buffer += fileName.substring(Math.max(0, fileNameLength - 36), fileNameLength);
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
        spinnerTypeNoEvent = true;
        spinnerType.setSelection(type - 1);
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
            spinnerSizeNoEvent = true;
            spinnerSize.setSelection(sizeIndex);
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
        String filename = getPortCfgString(nActPort, portModuleIndex, PORT_DATA_FILENAME);
        editTextFile.setText(filename);
        editTextFile.setEnabled(bFilename);
        buttonBrowse.setEnabled(bFilename);

        // hpil interface or hard wired address
        if (type == 4 /*TYPE_HPIL*/) { // HPIL interface

            spinnerHardAddr.setEnabled(false);

            String addrOut = getPortCfgString(nActPort, portModuleIndex, PORT_DATA_ADDR_OUT);
            if (addrOut != null) { // first call
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
                int nIndex = getPortCfgInteger(nActPort, portModuleIndex, PORT_DATA_BASE);
                spinnerHardAddr.setSelection(nIndex);
                spinnerHardAddr.setEnabled(true);
            }
            else
                spinnerHardAddr.setEnabled(false);
        }
    }

    private boolean ApplyPort(int nPort) {

        int portModuleIndex = getPortCfgModuleIndex(nPort); // module in queue to configure

        // module type combobox
        int type = spinnerType.getSelectedItemPosition() + 1;
        setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_TYPE, type);

        // hard wired address
        setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_BASE, 0x00000);

        // filename
        String portCfgFileName = editTextFile.getText().toString();
        setPortCfgString(nPort, portModuleIndex, PORT_DATA_FILENAME, portCfgFileName);

        boolean bSucc = false;
        int dwChipSize = 0;
        int i;
        switch (type) {
            case 1: //TYPE_RAM
                if (portCfgFileName.length() == 0) { // empty filename field
                    // size combobox
                    i = spinnerSize.getSelectedItemPosition();
                    dwChipSize = getChipSizeFromSelectedPosition(i);
                    bSucc = (dwChipSize != 0);
                } else {								// given filename
//TODO
//                    LPBYTE pbyData;
//
//                    // get RAM size from filename content
//                    if ((bSucc = MapFile(psCfg->szFileName,&pbyData,&dwChipSize)))
//                    {
//                        // independent RAM signature in file header?
//                        bSucc = dwChipSize >= 8 && (Npack(pbyData,8) == IRAMSIG);
//                        free(pbyData);
//                    }
                }
                break;
            case 3: //TYPE_HRD
                // hard wired address
                i = spinnerHardAddr.getSelectedItemPosition();
                if(i == 0)
                    setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_BASE, 0x00000);
                else if(i == 1)
                    setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_BASE, 0xE0000);
                // no break;
            case 2: //TYPE_ROM
            case 4: //TYPE_HPIL
                // filename
//TODO
                bSucc = true; //MapFile(psCfg->szFileName,NULL,&dwChipSize);
                break;
            default:
                dwChipSize = 0;
                bSucc = false;
        }

        // no. of chips combobox
        //if ((i = (INT) SendDlgItemMessage(hDlg,IDC_CFG_CHIPS,CB_GETCURSEL,0,0)) == CB_ERR)
        //  i = 0;								// no one selected, choose "Auto"
        i = spinnerChips.getSelectedItemPosition();

        if (bSucc && i == 0) { // "Auto"
            int dwSize;

            switch (type)
            {
                case 1: //TYPE_RAM
                    // can be build out of 32KB chips
                    dwSize = ((dwChipSize % (32 * 2048)) == 0)
                            ? (32 * 2048) // use 32KB chips
                            : (     2048); // use 1KB chips

                    if (dwChipSize < dwSize) // 512 Byte Memory
                        dwSize = dwChipSize;
                    break;
                case 3: //TYPE_HRD
                case 2: //TYPE_ROM
                case 4: //TYPE_HPIL
                    // can be build out of 16KB chips
                    dwSize = ((dwChipSize % (16 * 2048)) == 0)
                            ? (16 * 2048) // use 16KB chips
                            : dwChipSize; // use a single chip
                    break;
                default:
                    dwSize = 1;
            }

            i = dwChipSize / dwSize; // calculate no. of chips
        }

        int chips = i; // set no. of chips
        setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_CHIPS, chips);

        if (bSucc) { // check size vs. no. of chips
            int dwSingleSize;

            // check if the overall size is a multiple of a chip size
            bSucc = (dwChipSize % chips) == 0;

            // check if the single chip has a power of 2 size
            dwSingleSize = dwChipSize / chips;
            bSucc = bSucc && dwSingleSize != 0 && (dwSingleSize & (dwSingleSize - 1)) == 0;

            if (!bSucc)
                Utils.showAlert(getContext(), "Number of chips don't fit to the overall size!");
        }

        if (bSucc) {
            setPortChanged(nPort, 1);
            setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_SIZE, dwChipSize);
            setPortCfgInteger(nPort, portModuleIndex, PORT_DATA_APPLY, 1);

            // set focus on "Add" button
            buttonAdd.requestFocus();
        }
        return bSucc;
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

    }

    private void OnEditTcpIpSettings(int portModuleIndex) {
    }

}
